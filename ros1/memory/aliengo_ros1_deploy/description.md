---
name: aliengo_ros1_deploy
scope: ros1/aliengo
status: active
last_updated: "2026-06-04 14:48 HKT"
owned_paths:
  - ros1/CMakeLists.txt
  - ros1/package.xml
  - ros1/include/aliengo_deploy/
  - ros1/src/
  - ros1/launch/
  - ros1/tx2_relay/
  - ros1/docker/
  - policy/aliengo/
read_when:
  - 修改 Aliengo ROS1 deployment runtime、TX2 relay、direct UDP、safety gates 或 launch 参数时
---

## Purpose

本 entry 记录 Aliengo ROS1 RL policy deployment 的稳定事实。`ros1/` 是 catkin package `aliengo_deploy`，面向 Unitree Aliengo v3.0.0 固件部署 locomotion policy。当前链路使用 ROG Docker / ROS Noetic 运行 `aliengo_deploy` node，并通过 TX2 relay 将外部 UDP command 转发到 Aliengo controller。

关键 runtime 边界：

- Docker 侧 `aliengo_deploy` 负责 policy inference、standing/walking gate、brake gate、force estimator、CSV logging、stand-up 前段。
- TX2 侧 `aliengo_relay` 使用 Unitree SDK v3.0.0 和 controller 通信。
- Aliengo controller 对外部 PC direct low-level UDP command 不可靠；当前 canonical route 是 `TX2 relay`。
- Policy runtime 通过 shared `ManagerBasedEnv` / `PolicySpec::MLP` 接入。

## When Codex/AI Should Read This Entry

- 修改 `aliengo_deploy` node、launch 参数、Aliengo constants、obs/action layout、PD gains 或 joint map。
- 修改 TX2 relay、direct UDP transport、remote controller decoding、brake/stand-up/force gate 行为。
- 排查 ROS1 Docker、Noetic、LibTorch、policy path 或 real robot startup 流程。

## Source Paths

- `ros1/README.md`
- `ros1/CMakeLists.txt`
- `ros1/package.xml`
- `ros1/include/aliengo_deploy/aliengo_constants.h`
- `ros1/include/aliengo_deploy/aliengo_deploy_node.h`
- `ros1/src/aliengo_deploy_main.cpp`
- `ros1/src/aliengo_deploy_node.cpp`
- `ros1/src/aliengo_udp_transport.cpp`
- `ros1/launch/aliengo_deploy.launch`
- `ros1/tx2_relay/aliengo_relay.cpp`
- `ros1/docker/Dockerfile`
- `policy/aliengo/`

## TODO Summary

- 首次或重新部署实机前，复核 joint map、PD gains、stand-up timing、hardware emergency stop 和 TX2 relay readiness。
- 如果 policy obs/action contract 改动，同步更新 `aliengo_constants.h`、ROS1 README 和 shared policy runtime memory。

## DONE Summary

- ROS1 `aliengo_deploy` package 已包含 policy inference、stand-up 前段、standing/walking gate、brake gate、force estimator、CSV logging 和 remote command handling。
- 已建立 TX2 relay route，用于绕过 Aliengo v3.0.0 controller 对外部 PC low-level UDP command 的限制。
- README 已记录部署步骤、controller 操作、launch 参数和安全注意事项。

## Recommended Next Files To Read

- `ros1/README.md`
- `ros1/include/aliengo_deploy/aliengo_constants.h`
- `ros1/src/aliengo_deploy_main.cpp`
- `ros1/src/aliengo_deploy_node.cpp`
- `ros1/tx2_relay/aliengo_relay.cpp`
- `memory/shared_policy_runtime/description.md`
