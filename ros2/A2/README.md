# a2_lowlevel

`a2_lowlevel` 是独立的 ROS2 ament package，用于标准 A2 low-level adapter。默认只 build A2 12 个 leg motors 的 low-level library 和 smoke，不修改 `ros2/src/**` 的 Go2W 链路。A2 policy deploy 是可选 target，需要显式打开 `BUILD_A2_POLICY_DEPLOY`。

## Build

先确保 Unitree SDK2 / ROS2 环境已经提供并 source 了 `unitree_hg` generated messages。

```bash
cd /Users/caobaoquan/Downloads/python/projects/AliengoSim2Real/ros2
colcon build --packages-select a2_lowlevel
source install/setup.bash
```

只 build low-level adapter / smoke 时不需要 LibTorch 或 jsoncpp。

## Optional Policy Build

`a2_policy_deploy` 通过 shared `utils/cpp_manager_env` 的 `ManagerBasedEnv` / `Policy` runtime 加载 TorchScript policy，但 low-level publish boundary 仍然只走 `A2LowLevelInterface::publish_joint_commands()`。

启用 policy target：

```bash
cd /Users/caobaoquan/Downloads/python/projects/AliengoSim2Real/ros2
colcon build --packages-select a2_lowlevel --cmake-args \
  -DBUILD_A2_POLICY_DEPLOY=ON
source install/setup.bash
```

如果 policy asset 不在默认位置，可覆盖 CMake default 或 ROS params：

```bash
colcon build --packages-select a2_lowlevel --cmake-args \
  -DBUILD_A2_POLICY_DEPLOY=ON \
  -DA2_POLICY_DEFAULT_PATH=/path/to/policy.pt \
  -DA2_POLICY_DEFAULT_JSON_PATH=/path/to/policy.json
```

默认 policy contract:

- TorchScript: `policy/A2_policy/policy.pt`
- JSON: `policy/A2_policy/policy.json`
- deploy control rate: `50 Hz`

## Deploy Machine Info

调整 A2 deployment chain 前，建议先在真实部署机上采集一次机器、网络、ROS2、Unitree repo 和 A2 package readiness 信息。该报告用于判断 deploy machine 的 ROS2 distro、CycloneDDS/RMW 配置、Unitree SDK2/ROS2 checkout 状态、`unitree_hg` interfaces、网卡网段和基础 build/runtime tools 是否满足 A2 low-level chain 的要求。

默认输出 Markdown 到 stdout，可重定向保存为 `DeployMachineINFO.md`：

```bash
cd /path/to/AliengoSim2Real
bash ros2/A2/scripts/collect_deploy_machine_info.sh > DeployMachineINFO.md
```

如果部署机已连接 Unitree/A2 网络，可额外执行短 ping 检测 `192.168.123.161`、`192.168.123.162`、`192.168.124.162`：

```bash
bash ros2/A2/scripts/collect_deploy_machine_info.sh --ping > DeployMachineINFO.md
```

如果 Unitree sources 不在默认 `$HOME/third_party/unitree`，显式指定 root：

```bash
bash ros2/A2/scripts/collect_deploy_machine_info.sh \
  --unitree-root /path/to/unitree \
  > DeployMachineINFO.md
```

脚本默认启用 `--no-sensitive` 行为，不 dump 全量 env；只输出 `ROS_DISTRO`、`RMW_IMPLEMENTATION`、`CYCLONEDDS_URI` 的受限摘要和必要 command/path/version 信息。采集失败的 probe 会标记为 `MISSING` / `UNAVAILABLE` / `FAILED` 并继续，适合在非 ROS 或 macOS 环境先做 smoke。

## Topic / Type

- Subscribe: `rt/lowstate`
- Type: `unitree_hg/msg/LowState`
- Publish: `rt/lowcmd`
- Type: `unitree_hg/msg/LowCmd`

`A2LowLevelInterface` 会保存最近一次 `LowState` 的 `mode_pr`、`mode_machine`、`tick`、IMU quaternion/gyroscope、前 12 个 joint q/dq 和 `wireless_remote[40]`。发送非零 joint command 时必须先收到 fresh `rt/lowstate`，否则拒绝发布并 log warn。

## 12 Motor Order

标准 A2 12 motor order 固定为：

| Index | Name |
| --- | --- |
| 0 | `FR_BODY` |
| 1 | `FR_THIGH` |
| 2 | `FR_CALF` |
| 3 | `FL_BODY` |
| 4 | `FL_THIGH` |
| 5 | `FL_CALF` |
| 6 | `RR_BODY` |
| 7 | `RR_THIGH` |
| 8 | `RR_CALF` |
| 9 | `RL_BODY` |
| 10 | `RL_THIGH` |
| 11 | `RL_CALF` |

代码中提供 `A2MotorIndex`、`kA2MotorOrder`、`kA2MotorNames` 常量。`publish_joint_commands()` 只写 `motor_cmd[0:12]`，`motor_cmd[12:35]` 会清零。

## A2 Policy Contract

`a2_policy_deploy` 启动时会读取并校验 `policy.json`：

- `action_dim = 12`
- `per_frame_obs_dim = 46`
- `history_length = 32`
- flattened observation dim = `1472`
- `action_scale = 0.25`
- `sim_dt = 0.005`
- `control_decimation = 4`
- `1 / (sim_dt * control_decimation) = 50 Hz`
- `joint_names` 和 `obs_joint_names` 必须等于训练顺序：
  `FL_hip, FR_hip, RL_hip, RR_hip, FL_thigh, FR_thigh, RL_thigh, RR_thigh, FL_calf, FR_calf, RL_calf, RR_calf`

每帧 observation order 固定为：

| Segment | Dim | Scale |
| --- | ---: | --- |
| `projected_gravity_xy` | 2 | 1 |
| `base_ang_vel` | 3 | `0.25` |
| `joint_q - default_pos` | 12 | 1 |
| `joint_dq` | 12 | `0.05` |
| `last_raw_action` | 12 | 1 |
| `gait_clock` | 2 | 1 |
| `command` | 3 | `[2, 2, 0.25]` |

History length 是 `32`，通过 `ManagerBasedEnv` observation terms 展平。`a2_policy_deploy` 不让 policy 直接写 `unitree_hg::msg::LowCmd`；policy output 先映射成 `std::array<A2JointCommand, 12>`，再交给 `A2LowLevelInterface` 处理 fresh-state guard、mode routing 和 CRC。

Command provider 可选 `static` 或 `remote`，最终进入 observation 的 command 仍按 `[2, 2, 0.25]` scale。gait clock 使用未 scale 的 active command 判断 standing：`abs(cmd_vx) < 0.1`、`abs(cmd_vy) < 0.1`、`abs(cmd_yaw) < 0.2` 时 gait phase reset/保持为 `0`，gait clock 为 `[0, 1]`；非 standing command 才按 `gait_frequency_hz / control_hz` 前进。

## A2 Remote Decode Contract

`a2_remote` 从 `wireless_remote[40]` decode A2 R3 remote state。stick 使用 Unitree SDK2 sample layout，全部是 little-endian `float32`：

| Stick | Offset |
| --- | ---: |
| `lx` | 4 |
| `rx` | 8 |
| `ry` | 12 |
| `ly` | 20 |

Button layout：

| Byte | Bits |
| --- | --- |
| `2` | `R1`, `L1`, `Start`, `Select`, `R2`, `L2`, `F1`, `F3` |
| `3` | `A`, `B`, `X`, `Y`, `Up`, `Right`, `Down`, `Left` |

Decode 后会先做 NaN/Inf guard；任何 stick float 非 finite 时 `A2RemoteState.valid=false`，remote stick 不参与 policy command。finite stick 会按 `remote_deadzone` 置零 deadzone 内输入，并 clamp 到 `[-1, 1]`。

## Policy Action Mapping

Policy raw action 使用训练 joint order。发送前按 `policy.json` 的 `action_clip` clip，然后转换为 position target：

```text
target_q = default_joint_pos + action_scale * clipped_raw_action
```

训练顺序到 A2 low-level order 的 mapping 固定为 same signs、no inversion：

| Training Joint | A2 Low-Level Joint |
| --- | --- |
| `FL_hip_joint` | `FL_BODY` |
| `FR_hip_joint` | `FR_BODY` |
| `RL_hip_joint` | `RL_BODY` |
| `RR_hip_joint` | `RR_BODY` |
| `FL_thigh_joint` | `FL_THIGH` |
| `FR_thigh_joint` | `FR_THIGH` |
| `RL_thigh_joint` | `RL_THIGH` |
| `RR_thigh_joint` | `RR_THIGH` |
| `FL_calf_joint` | `FL_CALF` |
| `FR_calf_joint` | `FR_CALF` |
| `RL_calf_joint` | `RL_CALF` |
| `RR_calf_joint` | `RR_CALF` |

PD gains 按 joint type 固定：

| Joint Type | `kp` | `kd` |
| --- | ---: | ---: |
| hip/body | 140 | 5 |
| thigh | 140 | 5 |
| calf | 220 | 9 |

`dq=0`、`tau=0`。

## Public API

- `A2LowStateSnapshot latest_state() const`
- `bool has_fresh_state(std::chrono::milliseconds timeout) const`
- `bool publish_zero()`
- `bool publish_joint_commands(const std::array<A2JointCommand, 12>& commands)`

`publish_joint_commands()` 使用最近 `LowState` 的 `mode_pr` 和 `mode_machine`，并要求 state age 不超过 `state_timeout_ms`，默认 `200 ms`。任何带非零 `q/dq/tau/kp/kd` 的 joint command 会强制使用 FOC `mode=0x01`。`publish_zero()` 可以在无 state 时发布安全 zero/stop command；无 state 时 `mode_pr=0`、`mode_machine=0`，有 state 时跟随最新 state。zero command 使用 `mode=0x00`，并且 `q/dq/tau/kp/kd` 全部为 `0`。

## Smoke Run

默认只监听，每秒打印 tick/mode/joints，不发布命令：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke
```

可选打印 remote decode，仍然不发布命令：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
  -p log_remote:=true \
  -p remote_deadzone:=0.08
```

低频发布 zero/stop command：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p publish_zero:=true
```

发布固定低刚度站立目标：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p stand_test:=true
```

参数：

- `publish_zero`：默认 `false`。为 `true` 时按 `command_hz` 发布 zero/stop command。
- `stand_test`：默认 `false`。为 `true` 时只在 fresh state 下发布固定低刚度站立目标；若同时设置 `publish_zero`，`stand_test` 优先。
- `state_timeout_ms`：默认 `200`。用于 fresh state 判断。
- `command_hz`：默认 `20`。用于 `publish_zero` 或 `stand_test` 的命令发送频率。
- `log_remote`：默认 `false`。为 `true` 时打印 decoded sticks 和 button names。
- `remote_deadzone`：默认 `0.08`。仅用于 remote decode logging。

## Policy Run

默认 `enable_motion=false`，即使 policy 加载成功也不会发布运动命令：

```bash
ros2 run a2_lowlevel a2_policy_deploy
```

显式启用 motion，并使用静态 command provider：

```bash
ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=true \
  -p cmd_vx:=0.0 \
  -p cmd_vy:=0.0 \
  -p cmd_yaw:=0.0
```

显式启用 motion，并使用 A2 R3 remote command provider：

```bash
ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=true \
  -p command_source:=remote \
  -p max_remote_vx:=0.4 \
  -p max_remote_vy:=0.25 \
  -p max_remote_yaw:=0.6 \
  -p remote_deadzone:=0.08
```

参数：

- `policy_path`：默认 `policy/A2_policy/policy.pt`。
- `policy_json_path`：默认 `policy/A2_policy/policy.json`。
- `enable_motion`：默认 `false`。为 `false` 时不发布 motion command。
- `command_source`：默认 `static`，可选 `static` / `remote`。
- `cmd_vx` / `cmd_vy` / `cmd_yaw`：static command provider，默认全 `0.0`。
- `max_remote_vx` / `max_remote_vy` / `max_remote_yaw`：remote stick 映射上限，默认 `0.4`、`0.25`、`0.6`。
- `remote_deadzone`：remote stick deadzone，默认 `0.08`。
- `state_timeout_ms`：默认 `200`，沿用 `A2LowLevelInterface` fresh-state 判断。

Remote mapping：

```text
cmd_vx  =  ly * max_remote_vx
cmd_vy  = -lx * max_remote_vy
cmd_yaw = -rx * max_remote_yaw
```

`ry` 不参与 command，只在 debug log 中保留。Remote 只作为 command provider 和 safety gate；不会直接写 `LowCmd`，policy output 仍然只经过 `A2LowLevelInterface::publish_joint_commands()`。

## Safety

真实硬件上使用 low-level command 前，必须确认 Unitree 内置运动控制服务 `ai_sports` / `ai_sport` 已关闭，否则底层服务可能不响应或发生控制冲突。当前不会自动 stand-up，也不会自动关闭 `ai_sport` / `ai_sports`。

`stand_test` 只是接口 smoke，不包含起身流程、姿态保护、limit check 或 emergency stop；首次运行应离地、限功率、有人值守，并准备硬件急停。

`a2_policy_deploy` 的 publish refusal 条件：

- `enable_motion=false`：node 仍监听 fresh `LowState`、更新 command provider、计算 observation 并 warm history，但在 `computeAction()` / `publish_joint_commands()` 前拒绝 motion publish
- missing/stale `rt/lowstate`
- `LowState`、observation 或 action 出现 `NaN` / `Inf`
- `command_source` 非 `static` / `remote`
- `command_source=remote` 时 remote stick decode invalid
- observation/action dimension 不符合 contract
- history 尚未 warm 到 `32` fresh frames

这些条件下 node 不发布 motion command。

Remote safety gate：

- `L2` 必须按住才允许 nonzero policy command；未按住时 active command 强制为 `[0,0,0]`。
- `Select` 或 `L2+B` 触发 local stop：清空 policy/history/action runtime，调用 `publish_zero()`，并要求 release 后重新 fresh-state + history warmup。这个 zero LowCmd 是显式 safe stop command，即使 `enable_motion=false` 也可能发布；不会发布 policy motion command。

## CRC

A2 CRC 在 `a2_crc` 中独立实现，不复用 Go2W CRC。实现按手册 `LowCmd_` layout 构造显式 raw struct：

`mode_pr, mode_machine, 35 raw MotorCmd, reserve[4], crc`

然后对 `crc` 之前的 32-bit words 计算 CRC，避免依赖 ROS2 generated message struct 的内存 layout。

## Policy Boundary

policy output 只映射成 `std::array<A2JointCommand, 12>` 并调用 `publish_joint_commands()`。policy 侧不直接写 `unitree_hg::msg::LowCmd`，也不绕过 fresh-state guard、mode routing 和 A2 CRC。

## Deploy Machine Validation Checklist

当前 A2 R3 remote layout 来自 Unitree SDK2 sample，仍需在部署机或实机 `rt/lowstate.wireless_remote[40]` 上验证：

- `a2_lowlevel_smoke --ros-args -p log_remote:=true` 能随 stick/button 变化打印对应 `lx/rx/ry/ly` 和 button names。
- `L2` gate release 时 `a2_policy_deploy command_source=remote` 只给 policy command `[0,0,0]`。
- `L2` held 时 mapping 符合预期方向：`ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
- `Select` 与 `L2+B` 会触发 local stop、发布 zero LowCmd，并要求 history 重新 warm 到 `32` fresh frames。
- 验证过程中继续保持离地或限功率、关闭 `ai_sport` / `ai_sports`、准备 hardware emergency stop。
