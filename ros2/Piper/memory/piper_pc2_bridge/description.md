---
name: piper_pc2_bridge
scope: ros2/Piper
status: hardware-validation-pending
last_updated: "2026-08-22 HKT"
owned_paths:
  - ros2/Piper/
read_when:
  - 修改 A2 PC2 PiPER bridge、SocketCAN ownership、ROS 2 arm interface、watchdog 或 remote manipulation adapter 时
---

## Purpose

本 entry 记录 DoorDog 的 A2 PC2 → PiPER communication bridge。最终 data path 是：笔记本 policy 经 A2 switch-1 的 `192.168.123.0/24` ROS 2 DDS 发送 semantic joint target；A2 PC2 在本机通过 PiPER 原厂 USB-CAN 和 `krushell/piper_sdk` 结束 CAN 链路。PC2 是唯一 CAN command owner，不转发 raw CAN。

## Current contract

- ROS namespace 固定为 `/piper`。
- `/piper/joint_states`: `sensor_msgs/msg/JointState`，关节名 `arm_j1` 至 `arm_j6`。
- `/piper/joint_command`: `trajectory_msgs/msg/JointTrajectory`，恰好一个 point，6 个 absolute joint positions，单位 rad。
- `/piper/diagnostics`: `diagnostic_msgs/msg/DiagnosticArray`。
- `/piper/resume`、`/piper/enable`、`/piper/stop`、`/piper/disable`: `std_srvs/srv/Trigger`。
- control rate 50 Hz，PiPER MOVE J speed 5%，command timeout 0.20 s，feedback timeout 0.50 s。
- command freshness 使用 PC2 monotonic receive time，不依赖 laptop 与 PC2 clock sync。
- bridge 启动后保持 disabled；enable 后必须收到新 command；任何 command/feedback fault 由 PC2 本地 quick stop 并 latch。恢复需要显式 resume，成功后仍保持 disabled，网络恢复不会自动恢复运动。
- PC2 bridge 使用 tested `krushell/piper_sdk` fork 的 `GetArmHighSpdInfoAverage`，在本地生成 20 ms motor-speed average。
- v1 不包含 gripper、MoveIt、trajectory queue、raw CAN tunnel 或 auto-enable。

## Deployment boundary

- PC2 推荐使用 Ubuntu 22.04 + ROS 2 Humble container；host 先使用 SDK 自带 script 把 PiPER 原厂 USB-CAN 配为 1 Mbit/s SocketCAN。
- container 使用 host network 访问 host `can0`，无需把 USB device 直接交给 container，也不在 container 内修改 CAN link。
- laptop 运行 policy、Torch/checkpoint 与 `PiperBridgeClient`；A2 base chain 仍走 PC1。
- `piper_krushell_manipulation` 仅用于复现已验证 task：它把原 `Manipulation` 对 `C_PiperInterface_V2` 的已用 subset 映射到 ROS 2，policy/FK/observation/history 仍来自原仓库。
- software quick stop 不是 safety-rated emergency stop；实机测试必须保留物理急停。

## Validation status

Local-only checks completed: Python compile, model/facade unit tests, shell syntax, package metadata. Docker image、ROS 2 graph、PC2 USB-CAN 与 real PiPER motion 尚未在本环境执行，因此 status 保持 `hardware-validation-pending`。
