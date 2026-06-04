# TODO

## 2026-06-04 14:48 HKT

- [ ] 在部署机验证 `colcon build --packages-select go2w_vtm --cmake-args -DUSE_ONNX=ON -DBUILD_TESTING=OFF`。
- [ ] 确认 ROS2 Humble、Unitree ROS2 messages、ONNX Runtime、camera topics、USB gamepad 和 Unitree wireless controller 与 `ros2/README.md` 一致。
- [ ] Policy family、action dimension、joint map 或 sensor topic 改动后，同步更新 `ros2/README.md` 和 shared policy runtime memory。
