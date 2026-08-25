# TODO

- [ ] A2前12轴raw mapping已由操作员逐轴只读观察确认PASS；继续复核sign、raw IMU `wxyz`、Stage2 projected-gravity语义以及local steady receipt-time age/skew。
- [ ] A2/PiPER joint identity/order/index已由操作员人工逐轴观察确认PASS；仍需在支撑状态下完成正式`joint-observe` evidence，并填写审阅direction/unit/zero/limits/stop表后签署`joint-validation` receipt。
- [x] PiPER-gate-aware two-A init候选image已定向build，真实state shadow ready且隔离command零消息；没有伪造live site或运行first-A live phase。
- [x] 已确认约1000 Hz bare-DDS `/lowcmd`来自宇树`ai_sport`；受保护MotionSwitcher release后mode为空且5秒无LowCmd；最终测试结束后已恢复`ai_sport`。
- [x] 操作员已完成physical checklist并批准physical。
- [x] ros-readonly、A2/PiPER baseline、dual live和最终`restore-a2`均完成并留存evidence。
- [ ] Two-stage stop的reset/A2 prone已实机通过；新build将PiPER第二段改为回first-A启动休息位后再stop，仍待下一次hardware验证。
- [x] 完成真实site与前置Gate后，dual live实机验证6秒init interpolation与0.60秒warmup。
- [x] 操作员检查 m45 offline evidence并记录`approve --gate offline`；AI未代签人工receipt。
- [x] A2/PiPER接线后确认`enp130s0 / 192.168.123.222/24`，PC1/PC2可达并完成A2-only ROS read-only probe。
- [x] 获得写操作许可后在PC2离线安装Docker/can-utils、SDK source与bridge image；保持CAN与bridge停止。
- [x] 获得CAN activation许可后配置唯一`can_piper`并完成feedback/diagnostics与ros-readonly Gate。
- [x] Physical与ros-readonly receipt均由操作员批准。
- [ ] PC2 command timeout、stop/resume与controller rate已实测；feedback-loss路径仍待单独验证。
- [x] A2/PiPER现场limits/rates已填入`config/site.yaml`，direct live parser与manifest取交集/最小值并完成live preflight。
- [ ] Network/read-only/baseline/fault/live-both已有receipts；Owner明确跳过joint-validation和分组件live receipt，不伪造缺失receipt。
