---
name: aliengo_deploy_docs
scope: ros1/docs
status: active
last_updated: "2026-06-04 14:48 HKT"
owned_paths:
  - ros1/README.md
  - ros1/scripts/deploy/
read_when:
  - 查找或更新 Aliengo ROS1 deploy docs、hardware notes、diagnostic tools 或 legacy/stale scripts path references 时
---

## Purpose

本 entry 记录 Aliengo deploy 文档的 canonical location。当前 Aliengo deploy docs 位于 `ros1/scripts/deploy/`。旧顶层 `scripts/` tree 是 legacy/stale 位置；不要恢复该顶层目录，遇到旧 `scripts/...` reference 时应改指向 canonical `ros1/scripts/deploy/...`，或在 `ros1/README.md` 内使用相对路径 `scripts/deploy/...`。

## When Codex/AI Should Read This Entry

- 查找 Aliengo ROS1 deploy environment、machine info、policy info、hardware notes、diagnostic tools、brake/stand/walk gate 文档。
- 更新 `ros1/README.md` 中 deploy docs 路由。
- 清理或判断 legacy/stale old top-level `scripts/...` reference，并改指向 canonical `ros1/scripts/deploy/...`。

## Source Paths

- `ros1/scripts/deploy/ros1ENV.MD`
- `ros1/scripts/deploy/machineINFO.md`
- `ros1/scripts/deploy/my_policyINFO.md`
- `ros1/scripts/deploy/ALIENGO_ROS1_REPLACEMENT_INTERFACE.md`
- `ros1/scripts/deploy/aliengo_hardware_notes.md`
- `ros1/scripts/deploy/diagnostic_tools.md`
- `ros1/scripts/deploy/docker_migration.md`
- `ros1/scripts/deploy/Stand-Walk_Gate.md`
- `ros1/scripts/deploy/brake.md`
- `ros1/scripts/deploy/StaticTest.md`
- `ros1/scripts/deploy/ROS1TEST.md`

## TODO Summary

- 新增或改名 Aliengo deploy docs 时，同步更新本 entry、`ros1/memory/MEMORY.md` 和 `ros1/README.md`。
- 如果发现旧顶层 `scripts/...` reference，标记为 legacy/stale 并改指向 `ros1/scripts/deploy/...`。

## DONE Summary

- 已将 Aliengo deploy docs canonical path 记录为 `ros1/scripts/deploy/`。
- `ros1/README.md` 已说明 canonical deploy docs location，并将 `ros1ENV.MD` reference 指向 `scripts/deploy/ros1ENV.MD`。

## Recommended Next Files To Read

- `ros1/README.md`
- `ros1/scripts/deploy/ros1ENV.MD`
- `ros1/scripts/deploy/machineINFO.md`
- `ros1/scripts/deploy/my_policyINFO.md`
- `ros1/scripts/deploy/ALIENGO_ROS1_REPLACEMENT_INTERFACE.md`
- `ros1/memory/aliengo_ros1_deploy/description.md`
