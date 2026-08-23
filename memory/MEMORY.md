# Global Memory

Updated: 2026-06-04 14:48 HKT

本目录记录跨 subsystem 复用的项目事实、routing 和 runtime design notes。不要把这里当作 ROS1/ROS2/A2 的详细施工记录；具体 robot deployment 仍回到各自 subsystem memory。

## Entries

- `project_overview/`
  - 项目整体定位、主要目录、robot deployment subsystem route。
  - 需要快速判断应该进入 ROS1 Aliengo、ROS2 Go2W、ROS2 A2 还是 shared runtime 时读取。
- `shared_policy_runtime/`
  - `ManagerBasedEnv`、`PolicySpec`、policy assets、observation/action runtime 的共享事实。
  - 修改 policy loading、inference device、observation terms、action terms、history buffer 或跨 robot policy contract 时读取。
- A2 + PiPER Stage2 dual-policy deployment: `../deploy/a2_piper_stage2/MEMORY.md`

## Downstream Routes

- ROS1 Aliengo: `../ros1/memory/MEMORY.md`
- ROS2 Go2W: `../ros2/src/memory/MEMORY.md`
- ROS2 A2: `../ros2/A2/MEMORY.md`
- A2 + PiPER Stage2: `../deploy/a2_piper_stage2/MEMORY.md`

## Notes

- `memory/` 只放全局 memory；不要在这里记录单个 robot 的临时 deploy log。
- 当 global README、policy runtime 或 shared utility 的事实改变时，优先更新对应 entry summary。
