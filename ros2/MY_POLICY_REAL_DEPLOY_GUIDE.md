# My Policy Real Deploy Guide

Important note:

- this file describes the current `go2w_real_deploy` stack in this repo
- it is a `ROS2 + unitree_go + Go2W` deployment path
- if the target robot is `Aliengo`, do not treat this file as the final transport/interface plan
- for Aliengo ROS1 replacement work, see `scripts/ALIENGO_ROS1_REPLACEMENT_INTERFACE.md`

This repo currently has two different deployment lines:

- `mujoco/C++/lab2mj.cpp`: sim2sim verification
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`: real robot deployment

If your goal is "export my own policy and deploy it on the robot", the real chain is the ROS2 one. `scripts/ros1ENV.MD` is only a ROS1/Noetic environment note and explicitly says the current repo is still ROS2.

## 1. What Runs Where

### This development machine

- write code
- export or place model artifacts under `policy/<name>/` or any external directory
- sync the repo or only the policy directory to the deployment machine

### The deployment machine

Environment snapshot in `scripts/machineINFO.md` shows:

- Ubuntu `22.04.5`
- `x86_64`
- ROS2 `humble` exists under `/opt/ros`
- NVIDIA GPU is available
- `jsoncpp` and `udev` are available

The same snapshot also shows two current blockers for real deploy:

- `UNITREE_ROS2_SETUP` is `unset`
- `~/unitree_ros2/setup_local.sh` is missing

It also does not show `onnxruntime` in `ldconfig -p`, so either:

- ONNX Runtime is not installed in a standard path, or
- you must pass `-DONNXRUNTIME_ROOT=...` during build

## 2. Real Deploy Call Chain

### Step 1: build-time default policy path

`ros2/src/CMakeLists.txt` bakes these default policy directories into the binary:

- `motion_tracking`
- `vtm`
- `vtm_lstm_sru`
- `vtm_gru_sru`

That means the compiled binary remembers absolute paths from the build machine. If the repo path changes after build, default loading can break. Runtime override arguments are the safer path for your own policy.

### Step 2: process entry

`go2w_real_deploy.cpp`:

- creates 4 `PolicySpec` entries
- parses runtime arguments like `device=cuda`
- parses per-policy override arguments like `vtm_lstm_sru=/abs/path/to/policy_dir`
- creates `Go2wRealDeployNode`
- calls `init_manager()`, `init_gamepad()`, `start()`

### Step 3: manager initialization

`ManagerBasedEnv::init_manager()` does three important things for each policy:

1. load the model
2. compute the observation dimension from the registered observation terms
3. run one dummy inference

If dummy inference fails, the process exits immediately. In practice this is your first compatibility gate for a new policy.

### Step 4: 50 Hz control loop

`Go2wRealDeployNode::low_cmd_write()` runs every `20 ms`:

1. apply pending policy/sensor reset and switch requests
2. refresh visual observations
3. call `manager_step(policy_id_)`
4. convert policy output to Unitree low-level command
5. publish `/lowcmd`

The node refuses to move before you explicitly enable output with controller `A`. Safe stop is bound to `B`.

## 3. What the Real Node Expects

### ROS topics

The real deploy node subscribes to:

- `/lowstate`
- `/wirelesscontroller`
- `/camera/depth/image_raw`
- `/camera/color/image_raw`

It publishes:

- `/lowcmd`

### Action shape

The deployed policy must output at least `16` floats.

The runtime maps them like this:

- `action[0:12]` -> 12 leg joint position targets
- `action[12:16]` -> 4 wheel velocity targets

If the action dimension is `< 16`, the node logs an error and sends zero command.

## 4. Loader Rules for Your Policy Directory

The model loader is shared by sim2sim and real deploy.

### Full model discovery

If you pass a directory path, the loader prefers:

- for MLP: `policy.*`, then `student.*`
- for SRU: `student.*`, then `policy.*`

For ONNX build, it searches `.onnx`.
For LibTorch build, it searches `.pt`.

It also checks:

- `<dir>/`
- `<dir>/exported/`

### SRU split discovery

If the policy is SRU split deployment, the loader looks for:

- `*_deploy.json`

and then resolves the referenced artifacts relative to that JSON file.

### Metadata rules

For SRU policies, runtime state metadata can come from:

- `student_info.json`
- `student_deploy.json`
- or explicit `PolicySpec::SRU(...)` fields

If the policy is recurrent but metadata is missing or mismatched, loading fails.

## 5. Current Compatible Policy Slots

The current real deploy node only recognizes these `PolicySpec.description` values:

- `motion_mlp`
- `vtm`
- `vtm_lstm_sru`
- `vtm_gru_sru`

Those descriptions are not cosmetic. They decide:

- which observation group gets registered
- whether the policy is treated as a visual policy
- which D-pad direction selects it
- whether default visual command warm-start behavior is applied

### Slot 0: `motion_mlp`

Observation layout from `registerManager1()`:

- motion: `24`
- motion_task: `1`
- motion_anchor_pos_b: `3`
- motion_anchor_ori_b: `6`
- base_ang_vel history `3`: `9`
- projected_gravity history `3`: `9`
- command history `1`: `3`
- dof_pos history `3`: `36`
- dof_vel history `3`: `48`
- last_action history `3`: `48`

Derived total input length: `187`

This slot is the least coupled to vision and recurrent state.

### Slot 1: `vtm`

Observation layout from `registerManager2()`:

- vector part: `265`
- image part: `9 x 18 x 32 = 5184`

Derived total input length: `5449`

`policy/vtm/student_info.json` matches this:

- `total_input_length = 5449`
- `policy_normal = 265`
- `policy_image = 9 x 18 x 32`

### Slot 2: `vtm_lstm_sru`

Observation layout from `registerManager3()`:

- vector part: `53`
- image part: `1 x 18 x 32 = 576`

Total input length: `629`

Current metadata in `policy/vtm_lstm_sru/student_deploy.json` requires:

- memory type: `lstm_sru`
- `num_layers = 1`
- `hidden_dim = 256`

### Slot 3: `vtm_gru_sru`

Observation layout from `registerManager4()` is the same as slot 2:

- vector part: `53`
- image part: `1 x 18 x 32 = 576`

Total input length: `629`

Current metadata in `policy/vtm_gru_sru/student_deploy.json` requires:

- memory type: `gru_sru`
- `num_layers = 1`
- `hidden_dim = 256`

## 6. When You Can Deploy Without Changing C++

You do not need to change code if your new policy can replace one existing slot exactly.

### Case A: replace `vtm`

Use this if your policy:

- is a non-recurrent visual policy
- expects total input length `5449`
- expects `265 + (9 x 18 x 32)` split
- outputs at least `16` actions

### Case B: replace `vtm_lstm_sru`

Use this if your policy:

- is LSTM-SRU style
- expects total input length `629`
- expects `53 + (1 x 18 x 32)` split
- uses `1 x 256` recurrent state
- can be loaded by the current SRU split loader

### Case C: replace `vtm_gru_sru`

Use this if your policy:

- is GRU-SRU style
- expects the same `629` input layout
- uses `1 x 256` recurrent state

## 7. When You Must Change C++

You must edit code if any of these are true:

- your policy needs a new `description` name
- your observation layout differs from all existing slots
- your action semantics are not `12 joint q + 4 wheel dq`
- your policy needs different action scaling or default offsets
- your visual policy should not follow the current `uses_visual_policy()` logic
- your camera topic names or preprocessing differ

The usual files are:

- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`

Typical change points:

- add a new `PolicySpec`
- add a new `registerManagerX()`
- update `initObsManager()`
- update `uses_visual_policy()`
- update action mapping in `low_cmd_write()` if action meaning changes

## 8. Recommended Artifact Layouts

### Non-recurrent MLP or visual MLP

Recommended directory:

```text
<my_policy_dir>/
  student.onnx
  student_info.json
```

Also accepted:

- `policy.onnx`
- another `.onnx` file under the directory if it sorts to the front

### SRU split ONNX

Recommended directory:

```text
<my_policy_dir>/
  student_deploy.json
  student_info.json
  student_encoder.onnx
  student_memory.onnx
  student_actor.onnx
  student.onnx
```

`student.onnx` is optional for forced split runtime, but keeping it helps reuse the same export bundle elsewhere.

## 9. Shortest Path to Deploy Your Own Policy

### Option 1: replace an existing slot without code changes

1. Export your model into a new directory, for example:

   ```text
   policy/my_vtm_lstm_sru/
   ```

2. Make sure its artifact format matches one of the compatible slots above.

3. Sync that directory to the deployment machine.

4. On the deployment machine, fix environment blockers first:

   - provide `~/unitree_ros2/setup_local.sh`, or set `UNITREE_ROS2_SETUP`
   - install ONNX Runtime or pass `-DONNXRUNTIME_ROOT=...`

5. Build:

   ```bash
   cd <repo>/ros2
   source /opt/ros/humble/setup.bash
   source ~/unitree_ros2/setup_local.sh
   colcon build --packages-select go2w_vtm --cmake-args -DUSE_ONNX=ON -DBUILD_TESTING=OFF
   source install/setup.bash
   ```

6. Launch with path override. Example for replacing slot `vtm_lstm_sru`:

   ```bash
   ./start_go2w_real_deploy_wireless_only.sh \
     device=cuda \
     vtm_lstm_sru=/abs/path/to/my_vtm_lstm_sru
   ```

   Or use the wrapper script added in this repo:

   ```bash
   ./start_go2w_real_deploy_with_policy.sh \
     vtm_lstm_sru \
     /abs/path/to/my_vtm_lstm_sru \
     device=cuda
   ```

7. Use controller D-pad to select the slot:

   - Up: `motion_mlp`
   - Right: `vtm`
   - Down: `vtm_lstm_sru`
   - Left: `vtm_gru_sru`

8. Keep output disabled until:

   - `/lowstate` is updating
   - `/wirelesscontroller` is updating
   - depth topic is normal for visual policies

9. Press `A` only when the robot is in a safe start posture.

### Option 2: add a brand-new policy slot

1. add a new `PolicySpec` in `go2w_real_deploy.cpp`
2. add matching observation registration in `go2w_real_deploy_node.cpp`
3. update any visual-policy behavior helpers
4. rebuild on the deployment machine

## 10. Practical Advice for Your Next Step

If your policy is already one of these three shapes:

- visual MLP like `vtm`
- LSTM-SRU like `vtm_lstm_sru`
- GRU-SRU like `vtm_gru_sru`

then do not touch C++ first. Export into a new directory and deploy by runtime override.

If your policy is not shape-compatible, the fastest safe next move is:

1. decide which existing slot it is closest to
2. diff its expected obs/action contract against that slot
3. only then change `go2w_real_deploy.cpp` and `go2w_real_deploy_node.cpp`

## 11. Relevant Files

- `scripts/machineINFO.md`
- `scripts/ros1ENV.MD`
- `ros2/README.md`
- `ros2/start_go2w_real_deploy_wireless_only.sh`
- `ros2/start_go2w_real_deploy_with_policy.sh`
- `ros2/src/CMakeLists.txt`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`
- `utils/cpp_manager_env/ManagerEnv.cpp`
- `utils/cpp_manager_env/net.cpp`
