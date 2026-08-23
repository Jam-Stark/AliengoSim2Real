# TODO

- [ ] 现场复核同一台 A2 的 joint mapping/sign、raw IMU `wxyz` 与 Stage2 projected-gravity 语义，以及 local steady receipt-time age/skew。
- [ ] 在支撑状态下运行 `joint-observe`，由现场负责人仅用既有approved单关节程序逐项填写并审阅mapping/direction/unit/zero/limits/stop表，然后签署`joint-validation` receipt。
- [ ] 在部署机重跑 bundle parity、CPU benchmark 和持续 mock/shadow。
- [ ] 在 PC2/PiPER 完成 hardware interface、CAN、watchdog、stop/recovery 和 joint limit 验证。
- [ ] 将 hardware-certified A2/PiPER limits/rates 填入 `config/site.yaml`；direct live parser 会与 manifest 取交集/最小值。
- [ ] 按 `docs/operator_runbook_zh_CN.md` 生成 network、read-only、baseline、joint-validation、fault、shadow 与分组件 live 现场 receipts。
