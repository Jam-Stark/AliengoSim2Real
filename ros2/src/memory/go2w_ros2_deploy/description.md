---
name: go2w_ros2_deploy
scope: ros2/go2w
status: active
last_updated: "2026-06-04 14:48 HKT"
owned_paths:
  - ros2/README.md
  - ros2/src/CMakeLists.txt
  - ros2/src/package.xml
  - ros2/src/include/common/
  - ros2/src/include/go2w_real_deploy/
  - ros2/src/src/common/
  - ros2/src/src/go2w_real_deploy/
  - ros2/src/src/go2w_stand/
  - ros2/src/src/deep_camera/
  - policy/motion_tracking/
  - policy/vtm/
  - policy/vtm_lstm_sru/
  - policy/vtm_gru_sru/
read_when:
  - 修改 Go2W ROS2 deployment、go2w_vtm build/runtime、policy switching、camera input 或 Unitree ROS2 topic handling 时
---

## Purpose

本 entry 记录 ROS2 Go2W `go2w_vtm` package 的部署事实。`ros2/src/` 是 ament package，当前安装 `go2w_real_deploy`、`go2w_stand_example`、`deep_camera` 三个 executable。`go2w_real_deploy` 是 real robot policy deployment 主入口，通过 shared `ManagerBasedEnv` 加载多 policy，并支持 local USB gamepad 与 Unitree wireless controller。

当前默认 build path 是从 repo root 进入 `ros2/` 后运行 `colcon build --packages-select go2w_vtm`。`USE_ONNX` 默认 ON；ONNX Runtime 不在系统路径时需要设置 `ONNXRUNTIME_ROOT` 或传入 CMake 参数。

## When Codex/AI Should Read This Entry

- 修改 `go2w_real_deploy` startup args、policy list、fixed D-pad policy mapping、runtime reset 或 performance monitor。
- 修改 `/lowstate`、`/lowcmd`、`/wirelesscontroller`、camera depth/RGB topics 或 Go2W joint/action mapping。
- 更新 ROS2 Go2W build/run docs、ONNX Runtime routing 或 `go2w_vtm` installed targets。

## Source Paths

- `ros2/README.md`
- `ros2/src/CMakeLists.txt`
- `ros2/src/package.xml`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`
- `ros2/src/include/go2w_real_deploy/go2w_real_deploy_node.h`
- `ros2/src/src/go2w_stand/go2w_stand.cpp`
- `ros2/src/src/deep_camera/deep_camera.cpp`
- `ros2/src/src/common/motor_crc.cpp`
- `policy/motion_tracking/`
- `policy/vtm/`
- `policy/vtm_lstm_sru/`
- `policy/vtm_gru_sru/`

## TODO Summary

- 在部署机上确认 ROS2 Humble、Unitree ROS2 messages、ONNX Runtime、camera topics 和 gamepad/wireless controller input 与 README 一致。
- Policy family、action dimension、joint map 或 sensor topic 变化时，同步更新 `ros2/README.md` 和 shared policy runtime memory。

## DONE Summary

- `go2w_vtm` 当前安装 `go2w_real_deploy`、`go2w_stand_example`、`deep_camera`。
- `go2w_real_deploy` 默认加载 `motion_mlp`、`vtm`、`vtm_lstm_sru`、`vtm_gru_sru` 四个 policy，并支持 startup path override。
- `ros2/README.md` 已移除 legacy/stale absolute Go2W links/commands，改为 repo-relative/current-root-safe references。

## Recommended Next Files To Read

- `ros2/README.md`
- `ros2/src/CMakeLists.txt`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy.cpp`
- `ros2/src/src/go2w_real_deploy/go2w_real_deploy_node.cpp`
- `ros2/src/include/go2w_real_deploy/go2w_real_deploy_node.h`
- `memory/shared_policy_runtime/description.md`
