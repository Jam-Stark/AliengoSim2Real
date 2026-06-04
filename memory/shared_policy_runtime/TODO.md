# TODO

## 2026-06-04 14:48 HKT

- [ ] 新增 robot policy adapter 前，记录 observation layout、action dimension、joint order、action scaling、PD gains 和 reset semantics。
- [ ] A2 接 policy 前，定义 `ManagerBasedEnv` adapter 与 `A2LowLevelInterface` 的边界，避免绕过 fresh-state guard、mode routing 和 A2 CRC。
- [ ] 如后续统一 TorchScript / ONNX metadata，补充 policy asset manifest 规则和验证命令。
