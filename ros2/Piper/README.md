# A2 PC2 → PiPER ROS 2 Bridge

这个 package 把 PiPER 原厂 USB-CAN 的**唯一硬件所有权**放在 A2 用户开发单元 PC2 上，并让笔记本 GPU 通过 A2 的 `192.168.123.0/24` 千兆网络发送 6 关节目标、接收关节状态。

```text
Laptop GPU 192.168.123.10
  ├─ Unitree SDK2 / DDS ───────────────> A2 PC1 192.168.123.161
  └─ /piper/* ROS 2 topics/services ──> A2 PC2 192.168.123.162
                                             │
                                             └─ can_piper → official USB-CAN → PiPER
```

桥接的是**关节命令和状态**，不是原始 CAN 帧。PC2 本地负责 CAN、使能、状态检查、命令超时和快速停止；笔记本断线后不依赖笔记本进程执行停止。

## Scope

当前版本严格覆盖已经跑通的 `krushell/piper_sdk` manipulation 任务所需能力：

- 6 个 PiPER 关节，位置命令，单位 rad；
- 50 Hz 控制周期；
- 20 ms 窗口的高速电机速度平均值；
- 5% MOVE J 速度；
- 显式 resume、enable、disable、quick stop；
- 200 ms command watchdog、500 ms feedback watchdog；
- 笔记本端可原样运行 `piper_sdk.deployment.manipulation.Manipulation` 的测试适配器。

当前不包含 gripper、轨迹队列、MoveIt、原始 CAN 隧道或自动开机使能。

## ROS 2 interface

| Name | Type | Direction | Semantics |
|---|---|---|---|
| `/piper/joint_states` | `sensor_msgs/JointState` | PC2 → laptop | 6 关节位置、20 ms 平均速度、最新 effort |
| `/piper/joint_command` | `trajectory_msgs/JointTrajectory` | laptop → PC2 | 恰好一个 point；6 关节绝对位置 |
| `/piper/diagnostics` | `diagnostic_msgs/DiagnosticArray` | PC2 → laptop | CAN/状态/频率/watchdog/command gate 状态 |
| `/piper/resume` | `std_srvs/Trigger` | laptop → PC2 | 显式发送 quick-stop recovery；成功后 command gate 仍关闭 |
| `/piper/enable` | `std_srvs/Trigger` | laptop → PC2 | 反馈健康后确认 motor enable 并打开 command gate；之后 200 ms 内必须收到命令 |
| `/piper/stop` | `std_srvs/Trigger` | laptop → PC2 | 重复发送 PiPER quick-stop CAN 指令、关闭 command gate；恢复需显式 resume |
| `/piper/disable` | `std_srvs/Trigger` | laptop → PC2 | 失能机械臂 |

关节名固定为 `arm_j1` … `arm_j6`，与 A2+PiPER URDF 一致。命令 topic 使用 depth 1、best-effort、volatile，避免网络抖动后重放积压目标；服务和 diagnostics 使用 reliable。

## Package build

```bash
cd <GeneralSim2Real>/ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select piper_bridge
source install/setup.bash
```

PC2 推荐使用 `docker/` 中固定的 ROS 2 Humble 镜像。完整步骤见 [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md)，分层实机验收见 [docs/VALIDATION.md](docs/VALIDATION.md)。

## Executables

```bash
# PC2 hardware owner
ros2 run piper_bridge piper_bridge --ros-args -r __ns:=/piper \
  --params-file <GeneralSim2Real>/ros2/Piper/config/piper_bridge.yaml

# Laptop, read-only by default
ros2 run piper_bridge piper_smoke_test

# Laptop, hold the measured startup pose and verify the stop path
ros2 run piper_bridge piper_smoke_test -- --move --hold-current

# Laptop, explicit zero-position motion smoke
ros2 run piper_bridge piper_smoke_test -- --move

# Only after a prior bridge quick stop: explicitly recover, then move
ros2 run piper_bridge piper_smoke_test -- --move --resume-before-enable

# Laptop, existing krushell manipulation task through PC2
ros2 run piper_bridge piper_krushell_manipulation -- \
  --checkpoint_path /path/to/checkpoint.pt \
  --device cuda:0 \
  --run_policy

# Add --resume_before_enable only when intentionally clearing a prior bridge quick stop.
```

`piper_krushell_manipulation` is a narrow test adapter: it supplies a ROS 2 implementation of the subset of `C_PiperInterface_V2` used by the existing task, so policy/checkpoint/observation logic stays in the already tested repository. New DoorDog deployment code should use `PiperBridgeClient` or the ROS 2 interface directly.

## Safety boundary

The bridge quick stop is a software mechanism, not a safety-rated emergency stop. A reachable physical emergency stop and a clear test area remain mandatory. Never run `piper_ros`, a direct `C_PiperInterface_V2` control script, and this bridge as concurrent PiPER command owners.
