# DONE

- 采用 PC2 local CAN termination + ROS 2 semantic bridge，明确不做 raw CAN tunnel。
- 新增 `piper_bridge` ament Python package、launch/config、standard ROS 2 topics/services 和 diagnostics。
- 实现 PC2-local explicit resume/enable gate、joint/status/high-speed feedback checks、command watchdog、feedback watchdog、quick stop latch。
- 实现 laptop `PiperBridgeClient`、read-only/motion smoke 和 `krushell/piper_sdk` manipulation facade/runner。
- 新增 PC2 Docker image/run scripts、official SDK CAN activation wrapper 与 environment collector。
- 新增完整 deployment 与 layered validation docs。
- joint names 已按 A2+PiPER URDF 使用 `arm_j1` 至 `arm_j6`。
- local Python compile、unit tests、shell syntax 与 package metadata checks 已通过。
