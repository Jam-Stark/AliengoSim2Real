# TODO

- [x] 在实际 A2 PC2 采集 OS、Docker、网卡、USB 与 SocketCAN report。
- [x] 验证用户标记USB【3】对应PC2 kernel USB path `1-6:1.0`，记录USB ID与serial。
- [x] 验证m45单网线可同时访问PC1 `192.168.123.161` 与PC2 `192.168.123.162`；`.124.162`只在PC2 `net1`本地存在。
- [x] 确认m45 domain 0经`enp130s0`可见A2/PC2 bare DDS graph；PC2 bridge尚未安装，后续launch必须显式domain 0与`eth0`。
- [x] 通过PC2 ROS 2 Humble bridge container与m45 policy-host container隔离，确认SDK/rclpy与Torch runtime的实际共存方式。
- [x] 获得写操作许可后在PC2离线安装Docker/can-utils、实际PiPER SDK source与bridge image。
- [x] 获得CAN activation许可后配置唯一`can_piper`。
- [x] 在command gate关闭状态完成PiPER CAN feedback、`/piper/joint_states`与diagnostics只读验证。
- [x] 操作员在无command输出下逐一人工移动六轴，确认`arm_j1..arm_j6` joint state mapping全部PASS。
- [x] 目标image已定向build并实机验证enable后的fresh hold、6秒同步init与dual stop路径。
- [ ] Enable/stop/resume和command timeout已在实机出现并验证恢复；feedback-loss路径仍待单独验证。
- [x] Known joint target baseline与相同checkpoint的dual-policy arm-goal轨迹均已实机完成。
- [ ] Joint/status 200 Hz、ROS joint state 50 Hz与policy 50 Hz已记录；完整jitter、stop latency及navigation background load仍待量化。
- [x] A2与PiPER standalone baseline通过后，low-speed combined dual live已完成。
- [ ] 实机稳定后再决定是否增加 command-gate-closed system service 与 gripper interface。
