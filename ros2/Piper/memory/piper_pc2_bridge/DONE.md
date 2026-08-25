# DONE

- 采用 PC2 local CAN termination + ROS 2 semantic bridge，明确不做 raw CAN tunnel。
- 新增 `piper_bridge` ament Python package、launch/config、standard ROS 2 topics/services 和 diagnostics。
- 实现 PC2-local explicit resume/enable gate、joint/status/high-speed feedback checks、command watchdog、feedback watchdog、quick stop latch。
- 实现 laptop `PiperBridgeClient`、read-only/motion smoke 和 `krushell/piper_sdk` manipulation facade/runner。
- 新增 PC2 Docker image/run scripts、official SDK CAN activation wrapper 与 environment collector。
- 新增完整 deployment 与 layered validation docs。
- joint names 已按 A2+PiPER URDF 使用 `arm_j1` 至 `arm_j6`。
- local Python compile、unit tests、shell syntax 与 package metadata checks 已通过。
- 2026-08-24从m45完成PC2只读实机盘点：主机/资源/双网口/ROS2、USB-CAN udev/serial、SocketCAN停止状态及software缺口均已记录，未改变PC2或PiPER状态。
- 2026-08-24从m45构建并离线部署PC2 bridge环境：Docker29.7.2、Compose5.5.0、can-utils、SDK source与bridge image验证PASS；未配置CAN、未启动bridge、未发布command。
- 2026-08-24获批完成1 Mbit/s`can_piper`与command-gate-closed bridge，实时6关节state 50 Hz、diagnostics与Stage2 ros-readonly Gate PASS；未调用motion service或发布command。
- 2026-08-24 21:05 HKT，操作员在command gate关闭、无joint command时逐一人工移动六轴，确认`arm_j1..arm_j6` position映射全部PASS；完整direction/zero/limits/stop Gate仍未完成。
- 2026-08-25 final dual live完成PiPER自动resume/enable、同步init和两次显式14秒arm-goal轨迹；formal stop后bridge停止，A2恢复官方mode。
