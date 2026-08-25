---
name: piper_pc2_bridge
scope: ros2/Piper
status: dual-live-pass-bridge-stopped
last_updated: "2026-08-25 00:55 HKT"
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
- bridge 启动后 command gate 关闭；enable 在确认 motor enable 后打开 gate，随后必须收到新 command；任何 command/feedback fault 由 PC2 本地 quick stop 并 latch。恢复需要显式 resume，成功后 gate 仍关闭，网络恢复不会自动恢复运动。bridge gate 不等同于 PiPER motor driver enable bit。
- PC2 bridge 使用 tested `krushell/piper_sdk` fork 的 `GetArmHighSpdInfoAverage`，在本地生成 20 ms motor-speed average。
- v1 不包含 gripper、MoveIt、trajectory queue、raw CAN tunnel 或 auto-enable。

## Deployment boundary

- PC2 推荐使用 Ubuntu 22.04 + ROS 2 Humble container；host 先使用 SDK 自带 script 把 PiPER 原厂 USB-CAN 配为 1 Mbit/s SocketCAN，并命名为 `can_piper` 以避免占用 A2/PC2 可能已有的 `can0`。
- container 使用 host network 访问 host `can_piper`，无需把 USB device 直接交给 container，也不在 container 内修改 CAN link。
- laptop 运行 policy、Torch/checkpoint 与 `PiperBridgeClient`；A2 base chain 仍走 PC1。
- `piper_krushell_manipulation` 仅用于复现已验证 task：它把原 `Manipulation` 对 `C_PiperInterface_V2` 的已用 subset 映射到 ROS 2，policy/FK/observation/history 仍来自原仓库。
- software quick stop 不是 safety-rated emergency stop；实机测试必须保留物理急停。

## Validation status

- 2026-08-25 dual live完成first-A resume/enable、同步init、PolicyActive及两次显式轨迹；Stage2 live evidence为`evidence/live-both/20260825_004737_129916`。
- 旧second-L2+B立即stop PiPER，实测没有回first-A启动休息位。新policy-host build改为同步回启动休息位后stop，尚待下一次hardware验证；PC2 bridge contract/image不变。
- 当前测试已结束，PC2 bridge container停止；A2已恢复官方`ai_sport`。

- 实机PC2只读盘点见`docs/PC2_READONLY_20260824.md`：Ubuntu 22.04.4 RT kernel、i7-1355U、30 GiB RAM、`.123.162`/`.124.162`双网口、ROS2 Humble/CycloneDDS已确认。
- 用户标记USB【3】上的PiPER USB-CAN已枚举为`1d50:606f`、serial `003100365343570F20363330`、driver `gs_usb`、kernel path `1-6:1.0`、interface `can0`。
- 当前`can_piper`为1 Mbit/s UP/ERROR-ACTIVE且无bus error；bridge运行、command gate关闭，`/piper/joint_states`稳定50 Hz。
- M45通过`.123.0/24`同时访问PC1`.161`和PC2`.162`，domain 0可见A2 graph；`/lowstate`约1052.7 Hz。`.124.162`是PC2 `net1`地址，不是m45当前runtime直达路径。
- PC2当前没有default route；后续SDK/image/offline packages应由m45准备后经`.123.162`传入，不擅自修改PC2系统网络。
- PC2 bootstrap、CAN feedback、bridge launch与ros-readonly已PASS；尚未执行任何PiPER motion，下一步是physical与ros-readonly人工approval。
- 2026-08-24 21:05 HKT，操作员在command gate关闭且无joint command时逐一人工移动PiPER关节，确认`arm_j1..arm_j6` state mapping全部PASS；direction/zero/limits/stop仍待正式Gate。
- Stage2 direct pre-policy lifecycle已改为订阅`/piper/diagnostics`：first A后发送measured-position hold，人工enable后的fresh hold先接管watchdog，只有fresh `command_gate_open=true`才开始300-tick manifest-init插值。bridge本身仍不auto-enable，PC2 local stop/latch语义不变；新policy-host代码尚未目标build或实机enable。
