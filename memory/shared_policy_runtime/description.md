---
name: shared_policy_runtime
scope: global
status: active
last_updated: "2026-06-05 15:07 HKT"
owned_paths:
  - utils/cpp_manager_env/
  - policy/
  - ros1/src/aliengo_deploy_main.cpp
  - ros1/include/aliengo_deploy/
  - ros2/src/src/go2w_real_deploy/
  - ros2/src/include/go2w_real_deploy/
  - ros2/A2/
read_when:
  - 修改 shared policy loading、ManagerBasedEnv、PolicySpec、observation/action contract 或 policy assets 时
---

## Purpose

本 entry 记录跨 robot 复用的 policy runtime 事实。`utils/cpp_manager_env/` 提供 `ManagerBasedEnv`、`PolicySpec`、`ObservationTerm`、`ActionObsTerm`、`ActionTerm`、history buffer 和 inference wrapper；ROS1 Aliengo、ROS2 Go2W 与 ROS2 A2 policy deploy 都通过 `PolicySpec` 构造 policy list，并在 node 初始化时调用 `init_manager()`。

当前 policy assets 在 `policy/` 下维护，既有 TorchScript `.pt` 也有 ONNX `.onnx`。ROS2 Go2W 默认加载 `motion_tracking`、`vtm`、`vtm_lstm_sru`、`vtm_gru_sru` 四个 policy family；ROS1 Aliengo 默认使用 Aliengo policy path，可通过 launch/arg 覆盖；ROS2 A2 optional target 默认使用 `policy/A2_policy/policy.pt` 与 `policy/A2_policy/policy.json`。

## When Codex/AI Should Read This Entry

- 修改 `ManagerBasedEnv`、`PolicySpec`、inference device routing 或 policy state reset 行为。
- 调整 observation layout、history length、last action、command obs、image/ray obs 或 action scaling。
- 增加新 robot policy adapter，尤其需要复用 shared runtime 但保持 robot-specific safety boundary。
- 排查 policy path、TorchScript/ONNX loading、CPU/CUDA runtime 或 multi-policy switching 问题。

## Source Paths

- `utils/cpp_manager_env/ManagerEnv.hpp`
- `utils/cpp_manager_env/ManagerEnv.cpp`
- `utils/cpp_manager_env/net.h`
- `utils/cpp_manager_env/net.cpp`
- `utils/cpp_manager_env/Buffer.hpp`
- `policy/`
- `ros1/src/aliengo_deploy_main.cpp`
- `ros1/include/aliengo_deploy/aliengo_constants.h`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`
- `ros2/src/include/go2w_real_deploy/go2w_real_deploy_node.h`
- `ros2/A2/include/a2_lowlevel/a2_policy_deploy_node.h`
- `ros2/A2/src/a2_policy_deploy_node.cpp`
- `policy/A2_policy/policy.json`

## TODO Summary

- 如后续新增非 A2 robot policy adapter，接入前补齐明确的 policy contract：observation layout、action dimension、joint order、action scaling、PD gains、reset semantics。
- 如后续统一 TorchScript / ONNX metadata，补充 policy asset manifest 规则和验证命令。

## DONE Summary

- Shared C++ runtime 已被 ROS1 Aliengo 和 ROS2 Go2W 复用。
- Go2W runtime 已支持 multi-policy `PolicySpec`、ONNX build option、policy path override、policy performance monitor 和 controller-based fixed policy switching。
- Aliengo runtime 已通过 `PolicySpec::MLP` 接入 single-policy locomotion，并保留 `gate_preset`、`gait_frequency`、`inference_device` 等 runtime options。
- A2 Policy Adapter v1 已通过 `ManagerBasedEnv` / `PolicySpec::MLP` 接入 TorchScript policy，并明确 `A2LowLevelInterface` 是唯一 low-level publish boundary。

## Recommended Next Files To Read

- `utils/cpp_manager_env/ManagerEnv.hpp`
- `utils/cpp_manager_env/net.h`
- `ros1/memory/aliengo_ros1_deploy/description.md`
- `ros2/src/memory/go2w_ros2_deploy/description.md`
- `ros2/A2/memory/a2_deploy_progress/description.md`
