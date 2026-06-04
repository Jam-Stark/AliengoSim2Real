# ROS2 Go2W Memory

Updated: 2026-06-04 14:48 HKT

本目录记录 `ros2/src/` Go2W ROS2 deployment package 的可复用项目事实、当前 TODO 和已完成事项。A2 独立链路在 `../../A2/MEMORY.md`，不要把 Go2W memory 当作 A2 runtime 事实来源。

## Entries

- `go2w_ros2_deploy/`
  - `go2w_vtm` package、`go2w_real_deploy`、`go2w_stand_example`、`deep_camera`、ONNX/Torch runtime、Unitree ROS2 topics、camera/gamepad/policy switching。

## Routing

- Go2W real robot deployment、policy switching、ONNX Runtime、Unitree Go topics 或 camera/gamepad behavior，先读 `go2w_ros2_deploy/description.md`。
- 需要判断当前 blocker 或下一步施工时，再读同 entry 的 `TODO.md` 和 `DONE.md`。
- Shared policy runtime 变化需同步参考 `../../../memory/shared_policy_runtime/description.md`。
- A2 标准版 low-level work 走 `../../A2/MEMORY.md`，不在本 memory 维护。
