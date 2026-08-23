---
name: project_overview
scope: global
status: active
last_updated: "2026-06-04 14:48 HKT"
owned_paths:
  - README.md
  - memory/
  - ros1/
  - ros2/
  - policy/
  - utils/
  - mujoco/
  - robot/
read_when:
  - 需要了解 repo 整体结构、robot deployment subsystem routing 或 global docs 入口时
---

## Purpose

本 entry 记录 AliengoSim2Real 的全局定位：它是一个 general robot policy deployment framework，而不是只服务 Aliengo 或 Go2W 的单一机器人项目。repo 同时包含 MuJoCo 侧实验/工具、共享 policy assets、C++ policy runtime、ROS1 Aliengo deployment、ROS2 Go2W deployment，以及 ROS2 A2 low-level adapter 起点。

主要 subsystem route：

- ROS1 Aliengo deployment: `ros1/memory/MEMORY.md`
- ROS2 Go2W deployment: `ros2/src/memory/MEMORY.md`
- ROS2 A2 low-level deployment: `ros2/A2/MEMORY.md`
- A2 + PiPER Stage2 dual-policy deployment: `deploy/a2_piper_stage2/MEMORY.md`
- Shared policy runtime: `memory/shared_policy_runtime/description.md`

## When Codex/AI Should Read This Entry

- 需要判断当前任务属于哪个 robot/runtime subsystem。
- 更新 top-level README、memory routing 或项目结构说明。
- 新增 robot deployment path、policy family、shared utility 或 simulator bridge 前，需要确认现有边界。

## Source Paths

- `README.md`: global 项目入口。
- `policy/`: robot policy assets，包含 TorchScript / ONNX / metadata。
- `utils/cpp_manager_env/`: shared C++ policy runtime。
- `mujoco/`: MuJoCo C++ / Python experiment area。
- `robot/`: robot description assets。
- `ros1/`: Aliengo ROS1 deployment package。
- `ros2/src/`: Go2W ROS2 `go2w_vtm` package。
- `ros2/A2/`: A2 ROS2 low-level adapter package。
- `deploy/a2_piper_stage2/`: LMP Stage2 dog+arm direct C++ runtime、真实 policy bundle、offline parity/mock、Docker 与 site/Gate verification；A2 复用同机 `A2LowLevelInterface`，实机状态仍只认现场 receipts。
- `ros2/A2_Guide/`: A2 SDK/reference docs，memory 只引用该目录，不复制长文档。

## TODO Summary

- 新增 robot、runtime 或 deployment subsystem 时，同步更新 root `MEMORY.md`、global README 和对应 subsystem memory。
- 后续如果建立统一 build/test matrix，需要在 global README 中补充各 subsystem 的 verified environment。

## DONE Summary

- 已建立顶层 memory routing，覆盖 global、ROS1 Aliengo、ROS2 Go2W、ROS2 A2 四类入口。
- 已增加 A2 + PiPER Stage2 dual-policy deployment route，保持 A2/PiPER 底层 subsystem ownership 不变。
- Global README 已转为 general robot policy deployment framework 入口，避免只描述单一机器人。

## Recommended Next Files To Read

- `MEMORY.md`
- `memory/shared_policy_runtime/description.md`
- `ros1/memory/MEMORY.md`
- `ros2/src/memory/MEMORY.md`
- `ros2/A2/MEMORY.md`
