# TODO

- [ ] 在实际 A2 PC2 采集 OS、Docker、网卡、USB 与 SocketCAN report。
- [ ] 验证 USB-C `[2]` 对应 PC2，并记录稳定 USB bus address。
- [ ] 验证端口 `[7]` 单网线可同时访问 PC1 `192.168.123.161` 与 PC2 `192.168.123.162`；若 SSH 路径不同，记录 management path。
- [ ] 确认 laptop、A2 chain 与 PC2 bridge 使用相同 `ROS_DOMAIN_ID` 和正确 physical NIC。
- [ ] 确认已跑通的 krushell/PyTorch 环境与 ROS 2 Humble `rclpy` 的实际共存方式。
- [ ] 构建 PC2 image，完成 read-only state/diagnostics validation。
- [ ] 验证 enable→stop→resume、command timeout、feedback loss 三条 PC2-local stop/recovery path。
- [ ] 完成 known joint target smoke，再运行相同 checkpoint 的 remote manipulation test。
- [ ] 记录 state rate/jitter、policy period、stop latency 与 Unitree navigation background load。
- [ ] A2 与 PiPER 单独通过后再做 low-speed combined test。
- [ ] 实机稳定后再决定是否增加 command-gate-closed system service 与 gripper interface。
