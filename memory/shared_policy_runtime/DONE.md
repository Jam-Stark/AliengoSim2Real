# DONE

## 2026-06-04 14:48 HKT

- [x] 确认 `utils/cpp_manager_env/` 是 ROS1 Aliengo 与 ROS2 Go2W 共用的 C++ policy runtime。
- [x] 记录 `ManagerBasedEnv`、`PolicySpec`、observation terms、action terms、policy assets 的跨 subsystem 入口。
- [x] 记录 Go2W multi-policy 和 Aliengo single-policy runtime 的当前差异。

## 2026-06-05 15:07 HKT

- [x] 记录 A2 Policy Adapter v1 接入 shared `ManagerBasedEnv` / `PolicySpec::MLP` runtime，并使用 `policy/A2_policy/policy.pt` 与 `policy/A2_policy/policy.json`。
- [x] 定义 A2 `ManagerBasedEnv` adapter 与 `A2LowLevelInterface` 的边界：policy 只输出 `A2JointCommand[12]`，不直接写 `unitree_hg::msg::LowCmd`，不绕过 fresh-state guard、mode routing 或 A2 CRC。
- [x] 记录 A2 observation/action contract、joint order、action scaling、PD gains 和 publish safety gating。
