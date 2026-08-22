# PiPER Bridge Memory

Updated: 2026-08-22 HKT

本目录记录 A2 用户开发单元 PC2 作为 PiPER 原厂 USB-CAN hardware owner 的 ROS 2 deployment chain。它与 `../A2/` 的底盘链路并行：PC1 继续负责 A2，PC2 独占 PiPER CAN，笔记本通过同一 `192.168.123.0/24` DDS network 访问两者。

## Entries

- `memory/piper_pc2_bridge/`
  - `piper_bridge` package、ROS 2 interface、PC2 Docker、SocketCAN、watchdog、read-only/motion validation，以及 `krushell/piper_sdk` manipulation adapter。

## Routing

- 修改 `ros2/Piper/**`、`/piper/joint_states`、`/piper/joint_command`、PiPER enable/stop/disable services、PC2 USB-CAN ownership、remote manipulation runner 时，先读 `memory/piper_pc2_bridge/description.md`。
- 需要判断实机 blocker 和下一步时，再读同 entry 的 `TODO.md` 与 `DONE.md`。
- A2 low-level `/lowstate`、`/lowcmd`、MotionSwitcher 和底盘 policy 事实仍以 `../A2/MEMORY.md` 为准。
- PiPER hardware/manual 与 SDK reference 以随附资料和官方仓库为准，不复制长文档到 memory。
- 全局 routing 入口是 `../../MEMORY.md`。
