---
name: a2_deploy_progress
scope: ros2/A2
status: active
last_updated: "2026-06-04 14:48 HKT"
owned_paths:
  - ros2/A2/
  - ros2/A2_Guide/
read_when:
  - 修改 A2 ROS2 low-level deployment、unitree_hg interface、rt/lowstate/rt/lowcmd routing 或 deploy machine readiness 时
---

## Purpose

本 entry 记录 `ros2/A2` 的独立 A2 ROS2 low-level deployment 起点。当前 A2 work 不修改既有 Go2W `ros2/src/**` 链路。

已完成事实：

- 新建独立 ament package `a2_lowlevel`。
- 实现标准 A2 12-motor low-level adapter：订阅 `rt/lowstate`，发布 `rt/lowcmd`，使用 `unitree_hg` ROS2 messages。
- 实现 `A2LowLevelInterface` public API：`latest_state()`、`has_fresh_state()`、`publish_zero()`、`publish_joint_commands()`。
- 实现 A2 专属 CRC，未复用 Go2W `motor_crc`。
- 实现 `a2_lowlevel_smoke`，默认 listen-only，`publish_zero` / `stand_test` 需要显式参数。
- 实现部署机信息采集脚本 `ros2/A2/scripts/collect_deploy_machine_info.sh`，用于生成 `DeployMachineINFO.md`。
- 当前 code machine 已在 `~/third_party/unitree` clone `unitree_ros2`、`unitree_sdk2`、`unitree_sdk2_python`；部署机也计划使用相同路径。

当前 blocker：

- code machine 没有 ROS2 / `colcon` / `/opt/ros`，无法本地完整 build `a2_lowlevel`。
- A2 CRC 仍需和部署机 `unitree_hg` generated messages、Unitree SDK2 sample 或实机 low-level command 行为对照验证。
- 低层实机控制前必须确认 Unitree 内置运动服务 `ai_sport` / `ai_sports` 已关闭；当前首版只在 README / smoke log 提醒，未自动调用 `MotionSwitcherClient`。

## When Codex/AI Should Read This Entry

- 修改 A2 `a2_lowlevel` package、low-level adapter、CRC、smoke node 或 deploy machine info collector。
- 需要判断 A2 low-level deployment 当前 blocker、部署机验证步骤或安全前置条件。
- 后续接 policy 前，需要定义 A2 policy contract 与 `ManagerBasedEnv` adapter 边界。

## Source Paths

- `ros2/A2/package.xml`
- `ros2/A2/CMakeLists.txt`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/src/a2_lowlevel_interface.cpp`
- `ros2/A2/src/a2_crc.cpp`
- `ros2/A2/src/a2_lowlevel_smoke.cpp`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/README.md`
- `ros2/A2_Guide/`

## TODO Summary

- 等部署机运行 `collect_deploy_machine_info.sh` 生成 `DeployMachineINFO.md` 后，基于真实 ROS2 / Unitree / network / message interface 信息修正部署链路。
- 在部署机 build `a2_lowlevel`，验证 `unitree_hg` include/type/field names 和 CRC。
- 首次实机前确认 `ai_sport` / `ai_sports` 关闭、离地或限功率 smoke、hardware emergency stop。
- 后续接 policy 前定义 observation layout、action dimension、joint order、action scaling / PD gains、`ManagerBasedEnv` adapter 边界。

## DONE Summary

- A2 low-level adapter、smoke node、deploy machine info collector、README 首版已完成。
- A2 memory 已规范化为 root memory schema，并只引用 `ros2/A2_Guide/`，不复制长 A2 SDK docs。

## Recommended Next Files To Read

- `ros2/A2/README.md`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/memory/a2_deploy_progress/TODO.md`
- `ros2/A2/memory/a2_deploy_progress/DONE.md`
- `memory/shared_policy_runtime/description.md`
