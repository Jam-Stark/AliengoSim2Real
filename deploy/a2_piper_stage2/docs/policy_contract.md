# Stage2 policy contract and evidence boundary

## Authoritative files

Runtime 直接读取：

```text
policy_bundle/policy_manifest.yaml
policy_bundle/dog_actor.pt
policy_bundle/arm_actor.pt
policy_bundle/parity/dog_reference.npz
policy_bundle/parity/arm_reference.npz
```

接受的 manifest schema 是 `lmp_stage2_dual_policy_source_contract` version `1`。Site-specific topic、limit、watchdog 和 operation facts 只能写入 `config/site*.yaml`，不得反向改写 LMP source contract。

`metadata/lmp_source_contract.json` 中 `artifacts.policy_manifest.status=not_emitted` 指 downstream GeneralSim2Real schema 当时没有生成，而归档实际包含 LMP source-form `policy_manifest.yaml`。自动化集成时必须按这一语义处理，不能把两者混为同一个 downstream manifest 状态。

## Evidence status

### Independently verified

- ZIP payload inventory、文件 size 与路径结构一致。
- 两个 `.pt` 是 TorchScript archive；静态 graph 中只有预期的 linear/ELU/tanh/cat/slice/shape operations。
- dog 共有 `1,459,621` 个 float32 parameter values，arm 共有 `895,761` 个，静态读取全部 finite。
- 独立 NumPy forward 对 reference 的最大误差：dog `4.47034836e-7`，arm `4.76837158e-7`，均小于 `1e-6`。
- NPZ key/shape/dtype/finite、observation 拼接、29+1 preview、quaternion gravity、raw action、plan 和 nominal target 数值关系一致。

### Export receipt only

- authoritative runner filename `runner_state_020000.pt`、iteration `20000`、total timesteps `1,966,080,000`；
- saved TorchScript 与 runner actor 的 batch-3 direct comparison；
- export host CPU benchmark；
- MuJoCo simulation pass 声明。

原始 runner checkpoint、repository revision、resolved environment config、training logs 均未打包，因此不能把这些 receipt 字段升级为独立 lineage verification。用户已确认sim2sim与末态arm position error约`0.039 m`；它作为policy效果evidence接受而不作为deployment blocker，但不属于bundle机器可读lineage/parity字段。

## Actor shapes

| Actor | Input | Output | Internal graph |
| --- | --- | --- | --- |
| dog | `[B,1620]` | `[B,12]` | adaptation `[1620,256,128,25]`；actor `[1645,512,256,128,12]` |
| arm | `[B,600]` | `[B,8]` | adaptation `[600,256,128,9]`；history encoder `[580,512,256,128]`；actor `[157,512,256,128,8]` |

Arm output `[0:6]` 是 joint control；`[6:8]` 是模型内部已经 `tanh` 的 `[body_pitch, body_roll]` plan。

## Observation contract

Dog frame 是 54 维：

```text
[0:3]   projected_gravity_body
[3:15]  dog_joint_position - dog_default_position
[15:27] dog_joint_velocity_rad_s * 0.05
[27:39] previous raw dog action
[39:44] [vx, vy, yaw_rate, body_pitch, body_roll] * [2,2,0.25,1,1]
[44:50] [arm radius, elevation pitch, yaw, 0,0,0]
[50:52] base roll/pitch
[52:54] [sin(2*pi*phase), cos(2*pi*phase)]
```

Arm frame 是 20 维：

```text
[0:6]   arm_joint_position - arm_default_position
[6:12]  previous raw arm control action
[12:18] [arm radius, elevation pitch, yaw, 0,0,0]
[18:20] base roll/pitch
```

两侧 history 都是 30 帧、frame-major、oldest-to-newest。Reset 后以第一帧有效 observation 重复 30 次；全零 flat history 无效。

Projected gravity reference的semantic quaternion为root-link body-to-world active rotation、ROS `xyzw`。正式C++ direct path读取同一台A2 raw `wxyz`并直接应用等价公式；现场仍须核对IMU/body frame与gravity方向。

## Action contract

Dog joint order：

```text
FL_hip_joint, FR_hip_joint, RL_hip_joint, RR_hip_joint,
FL_thigh_joint, FR_thigh_joint, RL_thigh_joint, RR_thigh_joint,
FL_calf_joint, FR_calf_joint, RL_calf_joint, RR_calf_joint
```

Arm joint order是 `arm_j1` 至 `arm_j6`。两侧 nominal formula：

```text
nominal_target = clip(default_joint_position + 0.25 * raw_control,
                      -100 rad, +100 rad)
```

完整 deployment processing：

```text
raw actor output
  -> optional actor clip
  -> control slice and explicit named order
  -> default + 0.25 * raw
  -> broad training processed clip [-100,100]
  -> named LMP URDF joint limits
  -> site/hardware-certified joint limits
  -> per-tick target rate limit using previous limited target
  -> semantic JointTrajectory
  -> bridge unit conversion exactly once
```

### Nominal target 不等于 limited target

NPZ 的 `final_joint_target_rad` 实际对应上面的 `nominal_target`，即 site hard/rate limits 之前。Arm reference 中：

```text
arm_j5 nominal target: 1.4376565 rad
LMP manifest range:    [-1.22, 1.22] rad
```

所以它不能直接与 publish target 做 equal parity。NPZ 也没有 previous limited target，无法单独验证 rate-limit stage。Deployment 必须分别记录：

- `raw_actor_output`
- `nominal_policy_target`
- `hard_limited_target`
- `rate_limited_target`
- bridge 最终接收的 named target

`last_action` 只能使用第一项 raw output。

## Validator scope

Bundle 自带 `tools/validate_bundle.py` 是 model/reference validator，内部 hard-code shapes、default、scale 和 broad clip；它不解析 manifest，也不验证 site limits、joint order、watchdog 或 ROS transport。其 projected-gravity check 只覆盖 reference rotation matrix，且不调用 arm `forward_raw`。

Deployment 命令：

```bash
a2-piper-stage2 validate --bundle /policy_bundle --atol 1e-6
```

会同时运行：

- bundle 自带 model/reference parity；
- manifest schema/shape/observation slice validation；
- manifest-driven nominal target、plan、29+1 history、flatten order 与 `xyzw` gravity parity。

这仍是 offline contract verification，不证明 ROS transport、site mapping、state freshness、hard/rate-limited publish target或硬件行为。
