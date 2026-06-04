# Aliengo ROS1 Replacement Interface

This repo's current real-robot node is:

- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`

That stack is:

- ROS2
- `unitree_go` messages
- Go2W robot semantics

If the target platform is `Aliengo`, simply replacing the model path is not enough.
You need to replace the transport/interface layer with the ROS1 interface used by Unitree's Aliengo/A1-style stack, and you also need to replace the robot-specific action/observation layer where Go2W assumptions are hardcoded.

## 1. Bottom Line

For Aliengo deployment:

- reuse the inference core where possible:
  - `utils/cpp_manager_env/ManagerEnv.*`
  - `utils/cpp_manager_env/net.*`
  - `SimpleTensor`
  - `PolicySpec`
  - ONNX / LibTorch policy loading logic
- do not reuse the current ROS2 I/O layer as-is
- do not reuse the current Go2W robot semantics as-is

The correct direction is:

1. keep the policy runtime
2. replace ROS2 `unitree_go` communication with ROS1 `unitree_legged_msgs`
3. replace Go2W-specific robot/action wiring with Aliengo-specific wiring

## 2. Official ROS1 Interface for Unitree Legged Robots

From Unitree's ROS1 `unitree_ros_to_real` package:

- launch bridge:

  ```bash
  roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
  ```

- low-level ROS topics:
  - subscribe `low_cmd`
  - publish `low_state`

- message types:
  - `unitree_legged_msgs/LowCmd`
  - `unitree_legged_msgs/LowState`

The bridge node is `unitree_legged_real/src/exe/ros_udp.cpp`.

Its low-level branch does exactly this:

- `nh.subscribe("low_cmd", 1, lowCmdCallback);`
- `nh.advertise<unitree_legged_msgs::LowState>("low_state", 1);`

So the first direct replacement is:

- current ROS2 `/lowcmd` -> ROS1 `low_cmd`
- current ROS2 `/lowstate` -> ROS1 `low_state`

Compatibility note:

- the current public `unitree_ros_to_real` README is written around Go1 and older A1 releases
- `LowState.msg` still contains a note about old Aliengo versions lacking some extra Cartesian fields
- for actual Aliengo deployment, the final authority should be your local Aliengo manuals in `scripts/` plus the SDK/message version that matches that firmware generation

## 3. Replacement Map Against Current Repo

### Transport layer

Current repo:

- ROS2 `rclcpp`
- `unitree_go::msg::LowCmd`
- `unitree_go::msg::LowState`
- `unitree_go::msg::WirelessController`

Aliengo ROS1 replacement:

- ROS1 `roscpp`
- `unitree_legged_msgs::LowCmd`
- `unitree_legged_msgs::LowState`
- no standalone `WirelessController` topic in the official ROS1 package

### Topic layer

Current repo:

- `/lowcmd`
- `/lowstate`
- `/wirelesscontroller`
- `/camera/depth/image_raw`
- `/camera/color/image_raw`

Aliengo ROS1 replacement:

- `low_cmd`
- `low_state`
- controller data must be decoded from `LowState.wirelessRemote[40]`
- camera topics depend on the actual Aliengo sensor driver stack

### Build system

Current repo:

- `ament_cmake`
- `colcon build`

Aliengo ROS1 replacement:

- `catkin`
- `catkin_make`

### Startup flow

Current repo:

- `ros2 run go2w_vtm go2w_real_deploy`

Aliengo ROS1 replacement:

1. launch Unitree ROS1 UDP bridge:

   ```bash
   roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
   ```

2. run your own ROS1 deployment node in the same ROS1 graph

## 4. Most Important Interface Difference

The current ROS2 node expects a separate controller topic:

- `/wirelesscontroller`

Aliengo ROS1 official interface does not expose that as a separate message in `unitree_ros_to_real`.
Instead, the remote payload is embedded in:

- `unitree_legged_msgs/LowState.wirelessRemote[40]`
- `unitree_legged_msgs/HighState.wirelessRemote[40]`

So if you port the current logic, this part must change:

- remove the separate wireless-controller subscriber
- decode button/axis state from `low_state.wirelessRemote`

## 5. Why the Current Go2W Node Cannot Be Reused Directly

Even after replacing ROS2 with ROS1, the current node is still robot-specific.

### 5.1 Action semantics are Go2W-specific

Current `go2w_real_deploy_node.cpp` assumes:

- policy output has at least `16` values
- `0:12` are leg position targets
- `12:16` are wheel velocity targets

Aliengo is not Go2W. The official Unitree low-level examples control the first `12` leg motors and do not use the Go2W wheel semantics.

So for Aliengo, this part must be replaced:

- action dimension assumption
- mapping of action indices to motors
- any wheel-specific scaling or defaults

### 5.2 Joint map and default posture are Go2W-specific

Current node contains:

- `joint_map_`
- `obs_default_dof_pos_vec_`
- `act_default_dof_pos_vec_`
- `action_scale_vec_`
- `action2_scale_vec_`
- stop-posture targets

Those values are part of the robot definition, not generic policy runtime logic.

For Aliengo, all of these should be reviewed and likely rewritten.

### 5.3 Visual-policy assumptions are manual

Current node hardcodes visual-policy names:

- `vtm`
- `vtm_lstm_sru`
- `vtm_gru_sru`

and applies Go2W-specific defaults when those policies are active.

If your Aliengo policy is visual:

- keep the image-processing idea if it matches training
- but do not keep the current policy-name assumptions blindly

## 6. What Can Be Reused Safely

These parts are largely transport-agnostic and worth reusing:

- `PolicySpec`
- full-model and split-model artifact discovery
- ONNX Runtime / TorchScript loading
- recurrent-state reset logic
- observation history buffers
- action post-processing in `ManagerBasedEnv`, if the target action format matches

In short:

- policy loading logic is reusable
- ROS2 node code is not
- Go2W action/obs layout is not

## 7. What Must Be Replaced for Aliengo

You should plan for a new node or adapter layer, for example:

- `ros1/src/aliengo_real_deploy.cpp`
- `ros1/include/aliengo_real_deploy_node.h`
- `ros1/src/aliengo_real_deploy_node.cpp`

The new ROS1 node should do these jobs:

1. `ros::Publisher` to `low_cmd`
2. `ros::Subscriber` from `low_state`
3. optional image subscribers for Aliengo camera topics
4. decode `wirelessRemote[40]`
5. convert `LowState` into policy observations
6. convert policy output into Aliengo joint commands

## 8. Minimal ROS1 Porting Shape

At the API level, the port is straightforward:

- `rclcpp::Node` -> `ros::NodeHandle`
- `create_publisher<T>()` -> `nh.advertise<T>()`
- `create_subscription<T>()` -> `nh.subscribe()`
- ROS2 timer -> ROS1 control loop with `ros::Rate` or `ros::Timer`

The official ROS1 tutorial model is the standard `NodeHandle + advertise + subscribe + spin/spinOnce` pattern.

## 9. Practical Porting Plan

### Phase 1: isolate the reusable core

Keep:

- `ManagerBasedEnv`
- policy loading
- tensor helpers

Do not touch them yet.

### Phase 2: write a ROS1 Aliengo I/O shell

New node should first do only:

- subscribe `low_state`
- publish zero `low_cmd`
- confirm you can receive valid `LowState`

Note:

- in Unitree's official `ros_udp.cpp`, `low_state` is republished during the `low_cmd` callback path
- so continuously publishing `low_cmd` is part of the normal low-level loop

### Phase 3: port observation extraction

Replace current Go2W-specific observation getters with Aliengo equivalents:

- IMU
- projected gravity
- leg joint position
- leg joint velocity
- optional command vector
- optional image observations

### Phase 4: port action writer

Replace current `16`-dimensional Go2W writer with Aliengo's leg command writer.

### Phase 5: re-add controller logic

Rebuild:

- enable/stop
- policy switching
- reset

after you have a working `wirelessRemote[40]` decoder.

## 10. Recommendation for Your Policy Deployment Prep

If your own policy is intended for Aliengo:

- do not spend time polishing the current ROS2 `go2w_real_deploy` entrypoint
- spend time on the ROS1 replacement node and Aliengo robot contract

The fastest correct decomposition is:

1. keep model loading code
2. write Aliengo ROS1 node
3. make Aliengo obs/action contract match your policy
4. only then do on-robot deployment

## 11. Source Pointers

Local repo:

- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`
- `scripts/AlienGo 用户手册V1.1.pdf`
- `scripts/AlienGo Software Guide v2.0-zh.pdf`

Official Unitree ROS1 repo:

- `https://github.com/unitreerobotics/unitree_ros_to_real`

Particularly relevant files:

- `unitree_legged_real/launch/real.launch`
- `unitree_legged_real/src/exe/ros_udp.cpp`
- `unitree_legged_msgs/msg/LowCmd.msg`
- `unitree_legged_msgs/msg/LowState.msg`

Official ROS1 C++ tutorial:

- `https://wiki.ros.org/ROS/Tutorials/WritingPublisherSubscriber%28c%2B%2B%29`
