# A2 + PiPER Stage2 Deployment Memory

Updated: 2026-08-24 HKT

本目录记录 policy host 上的 LMP Stage2 dual-policy direct runtime。A2 与 main 成功 locomotion 路径一样在 C++ 进程内复用 `A2LowLevelInterface`，PiPER USB-CAN 仍由 `ros2/Piper/` 的 PC2 bridge 独占。

## Entry

- `memory/stage2_dual_policy/description.md`
  - 真实 policy bundle、dog/arm contract、arm-first same-tick preview、direct A2 path、offline verification、site config 和现场 Gate。
- 需要判断剩余现场项时读取同目录的 `TODO.md`；已完成事实见 `DONE.md`。

## Routing

- 修改 `deploy/a2_piper_stage2/**` 前先读本文件，再读 `memory/stage2_dual_policy/description.md`。
- 修改 raw A2 `LowState` / `LowCmd` 或 `A2LowLevelInterface` 时回到 `../../ros2/A2/MEMORY.md`。
- 修改 `/piper/*`、SocketCAN、PiPER watchdog 或 PC2 hardware owner 时回到 `../../ros2/Piper/MEMORY.md`。
- 全局入口是 `../../MEMORY.md`。
