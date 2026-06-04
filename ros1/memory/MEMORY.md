# ROS1 Aliengo Memory

Updated: 2026-06-04 14:48 HKT

本目录记录 `ros1/` Aliengo ROS1 deployment 的可复用项目事实、deploy docs routing、当前 TODO 和已完成事项。

## Entries

- `aliengo_ros1_deploy/`
  - `aliengo_deploy` package、TX2 relay、direct UDP、standing/walking gate、brake gate、policy inference、ROS launch。
  - 修改 ROS1 runtime code、deploy launch、TX2 relay 或实机安全流程时读取。
- `aliengo_deploy_docs/`
  - Aliengo deploy 文档 canonical location 和 legacy/stale 顶层 `scripts/` reference 处理。
  - 查找 `ros1ENV.MD`、hardware notes、diagnostic tools、replacement interface 或迁移文档时读取。

## Routing

- ROS1 Aliengo runtime/code 问题：先读 `aliengo_ros1_deploy/description.md`。
- ROS1 deploy 文档路径问题：先读 `aliengo_deploy_docs/description.md`。
- 需要判断 blocker 或下一步施工时，再读同 entry 的 `TODO.md` 和 `DONE.md`。
- Shared policy runtime 变化需同步参考 `../../memory/shared_policy_runtime/description.md`。
