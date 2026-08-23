# Stage2 已核实 policy contract

本部署目录直接读取真实 bundle 根目录中的 `policy_manifest.yaml`。运行时接受的 schema 是 `lmp_stage2_dual_policy_source_contract` version `1`；不再维护一份手工转写、可能漂移的 policy template。现场配置只放在 `config/site*.yaml`，不得反向修改 LMP contract。

## Bundle 与模型

- bundle manifest/receipt 声明的 authoritative checkpoint 是 `checkpoints_meta/runner_state_020000.pt`、iteration `20000`；该 runner checkpoint 未打包，所以下游可确认 `runner_role: training_state` contract，但不能独立复算 checkpoint lineage。
- `dog_actor.pt`：`torch.jit.script` 生成的 deterministic TorchScript，`forward(float32[B,1620]) -> float32[B,12]`。
- `arm_actor.pt`：`torch.jit.script` 生成的 deterministic TorchScript，`forward(float32[B,600]) -> float32[B,8]`；`[0:6]` 是 PiPER control，`[6:8]` 是模型内部已做 `tanh` 的 `[body_pitch, body_roll]` plan。
- dog history 是 `30 x 54`，arm history 是 `30 x 20`，均为 frame-major、oldest-to-newest。reset 后用第一帧有效实机 observation 重复填满 30 帧，不能以全零 history 启动。
- dog/arm `last_action` 都是上一 policy tick 的 raw actor output；arm history只缓存 control `[0:6]`，不缓存 plan。
- policy period 是 `0.02 s`。导出环境记录为 Python `3.11.15`、Torch `2.7.0+cu128`、NumPy `1.26.0`；deployment CPU candidate 是 Torch `2.7.0`，是否可用仍以目标 host parity 为准。

## Observation 与 same-tick protocol

dog 每帧 54 维：

```text
[0:3]   projected gravity in body/root-link frame
[3:15]  A2 q - default q
[15:27] A2 dq * 0.05
[27:39] previous raw dog action
[39:44] [vx, vy, yaw_rate, body_pitch_plan, body_roll_plan]
          * [2, 2, 0.25, 1, 1]
[44:50] [arm radius, elevation pitch, yaw, 0, 0, 0]
[50:52] base [roll, pitch]
[52:54] [sin(2*pi*phase), cos(2*pi*phase)]
```

arm 每帧 20 维：

```text
[0:6]   PiPER q - default q
[6:12]  previous raw arm control action
[12:18] [arm radius, elevation pitch, yaw, 0, 0, 0]
[18:20] base [roll, pitch]
```

每个 logical tick 必须先把同一状态快照提交到 arm/dog persistent histories，再运行 arm actor。新 plan 为 `clip(arm_output[6:8], -1, 1) * 0.4`。dog actor input 是 `committed_dog_history[1:30] + same-state/new-plan preview frame`；preview 不修改 persistent dog history。随后才运行 dog actor并缓存本 tick raw actions/plan，供下一状态快照使用。

Bundle/reference在semantic边界记录ROS `xyzw`；同一台A2 raw `LowState`实际按`wxyz`提取。C++ direct runtime直接读取`wxyz`并使用与reference等价的分量公式，不经过named semantic message；read-only Gate仍须核对IMU/body frame与projected-gravity方向。

## Action contract

dog joint order：

```text
FL_hip_joint, FR_hip_joint, RL_hip_joint, RR_hip_joint,
FL_thigh_joint, FR_thigh_joint, RL_thigh_joint, RR_thigh_joint,
FL_calf_joint, FR_calf_joint, RL_calf_joint, RR_calf_joint
```

arm joint order是 `arm_j1` 至 `arm_j6`。两侧 actor target 公式都是：

```text
pre_limit_target = clip(default_joint_position + 0.25 * raw_control,
                        -100 rad, +100 rad)
```

bundle parity 中的 `final_joint_target_rad` 指上式的 pre-site-limit target。它不包含 deployment joint-position limit、per-tick rate limit、bridge conversion 或 hardware target cache。部署 action path 应先复现该 parity stage，再与现场认证 limits 取交集并应用 manifest rate limit。LMP URDF limits 已记录在 `config/joint_inventory.from_lmp_urdf.yaml`，但不是 hardware-certified limits。

`arm_j7/j8` 没有 actor output；训练 target 固定为零，现有 PC2 bridge也没有 gripper command interface。禁止把 arm plan 两维解释成 gripper action。

## Parity 入口与边界

在解压后的 bundle 根目录运行：

```bash
python tools/validate_bundle.py . --atol 1e-6
```

安装本部署 package 后等价入口为：

```bash
a2-piper-stage2 validate --bundle /absolute/path/to/policy_bundle --atol 1e-6
```

bundle 自带的 validator 核对 actor input/output、pre-limit target、arm plan、arm-plan-to-dog preview、dog 29+1 history，以及 rotation-matrix 形式的 projected gravity。它不读取 policy manifest/source contract，shape/default/scale是脚本内固定值，也不从 quaternion 重算 projected gravity。部署 CLI 在此基础上增加 manifest-driven target/history/quaternion reference核对，但同样不读取现场 site config，也不证明 downstream ROS transport、joint mapping、watchdog、hold/stop 或硬件可用。

### 2026-08-24 独立接收验证

在 macOS arm64、Python 3.11、CPU Torch 2.7.0、NumPy 1.26.0 环境执行原 bundle validator，结果 `status: pass`：

```text
dog  [1,1620] -> [1,12]
arm  [1,600]  -> [1,8]

dog actor output max error        5.0663948e-7
arm actor output max error        9.5367432e-7
dog nominal target max error      1.1920929e-7
arm nominal target max error      2.3841858e-7
arm plan / dog preview max error  4.4703484e-8
history / gravity max error        0
```

全部不超过 `1e-6`。另一次不加载 pickle/TorchScript 的静态归档审计确认 19 个文件和 manifest size一致、参数 storage有限，并用 NumPy 直接复算 dog/arm MLP，最大误差分别为 `4.4703484e-7`、`4.7683716e-7`。这证明 bundle 可在另一 CPU/Torch 2.7.0 环境复现 reference，但不是目标 Linux policy host benchmark，也不是 hardware pass。bundle 自带的 `metadata/export_validation.json` 仍只算 export-side offline evidence。

用户已确认dual policy完成sim2sim，末态arm position error约`0.039 m`。该结论作为用户提供的policy效果evidence接受，不是deployment blocker；它不属于bundle机器可读parity字段，也不能替代hardware mapping/watchdog/Gate receipt。

## 当前可用 transport 边界

PiPER package 的 repository contract 是 `/piper/joint_states` (`sensor_msgs/msg/JointState`) 与 `/piper/joint_command` (`trajectory_msgs/msg/JointTrajectory`)；joint names为 `arm_j1..arm_j6`，command 是恰好一个 point 的 absolute radians。PC2 hardware validation仍 pending。

正式部署选择C++ direct mode，沿用main成功案例：node组合`A2LowLevelInterface`、读取进程内snapshot、使用fixed training mapping，且只调用existing `publish_joint_commands()`进入`/lowcmd`。所以laptop-facing named-joint A2 bridge不再是正式路径blocker。Python external-semantic `ros-shadow/ros-live`继续fail-fast；C++ direct shadow/live必须按operator runbook逐Gate生成site receipts后才可验收。
