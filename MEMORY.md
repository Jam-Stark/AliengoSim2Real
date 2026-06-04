# Project Memory

Updated: 2026-06-04 14:48 HKT

本文件是 AliengoSim2Real 的顶层 file-based memory 入口。开始实现、调试、review 或文档更新前，先从这里判断应读取哪个 subsystem memory；不要一次性读取全部 memory。

## Global Memory

- `memory/MEMORY.md`
  - 项目整体结构、通用 robot policy deployment framework 定位、shared policy runtime 事实。
  - 需要理解 `policy/`、`utils/cpp_manager_env/`、MuJoCo/ROS runtime 边界时读取。

## Subsystem Memory

- `ros1/memory/MEMORY.md`
  - ROS1 Aliengo deployment memory。
  - 处理 `aliengo_deploy`、TX2 relay、direct UDP、Aliengo deploy docs 时读取。
- `ros2/src/memory/MEMORY.md`
  - ROS2 Go2W `go2w_vtm` deployment memory。
  - 处理 Go2W real deploy、ONNX/Torch runtime、Unitree ROS2 topics、camera/gamepad/policy switching 时读取。
- `ros2/A2/MEMORY.md`
  - ROS2 A2 low-level deployment memory。
  - 处理 `a2_lowlevel`、`unitree_hg`、`rt/lowstate`、`rt/lowcmd`、A2 deploy machine readiness 时读取。

## Read Order

1. 先读本文件。
2. 根据任务选择一个最小必要 subsystem `MEMORY.md`。
3. 对具体 memory entry，先读 `description.md`。
4. 只有需要判断当前施工状态、blocker 或下一步时，再读同 entry 的 `TODO.md` 和 `DONE.md`。

## Maintenance Rules

- 新增 robot、runtime 或 deployment subsystem 时，同步更新本文件和对应 subsystem `MEMORY.md`。
- 完成 memory entry 中的 TODO 时，同次变更更新该 entry 的 `TODO.md`、`DONE.md` 和 `description.md` summary。
- 文档使用中文叙述 + English technical terms；稳定概念如 `ManagerBasedEnv`、`PolicySpec`、`Sim2SimEnv`、`TX2 relay`、`direct UDP`、`unitree_hg` 保持 English。
