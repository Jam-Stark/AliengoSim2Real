---
name: stage2_dual_policy
scope: deploy/a2_piper_stage2
status: direct-runtime-implemented-site-gated
last_updated: "2026-08-24 HKT"
owned_paths:
  - deploy/a2_piper_stage2/
read_when:
  - 修改 Stage2 bundle contract、dual-policy runtime、offline parity/mock、site config 或 coupled ROS boundary 时
---

## Purpose

该 entry 记录 A2 + PiPER Stage2 policy host。真实导出在 `policy_bundle/`，runtime 直接读取其中的 LMP-authoritative `policy_manifest.yaml`，不维护第二份转换 manifest。

## Verified contract

- dog TorchScript：`[B,1620] -> [B,12]`，history `54×30`。
- arm TorchScript：`[B,600] -> [B,8]`，history `20×30`；前 6 维控制 PiPER，后 2 维为模型内 `tanh` 后的 body pitch/roll plan。
- `sim.dt=0.005 s`、decimation `4`、policy period `0.02 s` / `50 Hz`。
- arm-first；dog input 使用 29 个 committed frame + 同状态、新 plan preview frame；preview 不改 persistent history。
- dog/arm action 均为 `default + 0.25 × raw` position offset；runner 是 `training_state`；gripper 无 actor output。
- bundle reference 的 `final_joint_target_rad` 是 site limit/rate limit 之前的 nominal target。LMP URDF limit 不等于 hardware-certified limit。

## Current boundary

- `ros2/Piper` 的 `/piper/joint_states`、`/piper/joint_command`、`arm_j1..arm_j6` absolute-radian interface 与 actor control维度对齐，但 hardware status 仍是 pending。
- 正式 C++ node 组合 `A2LowLevelInterface`，复用 raw `/lowstate`/`/lowcmd`、training mapping、PD、mode 和 CRC boundary；不再需要 laptop-facing named A2 semantic bridge。
- Python external-semantic transport 只作明确的 unavailable 边界；offline parity/runtime 仍是 oracle，实机 shadow/live 只走 `a2_piper_stage2_direct`。
- A2 raw IMU quaternion 是 `wxyz`；direct runtime 按同样顺序计算 Stage2 gravity/roll-pitch，仍需现场 read-only parity。
- Live startup 必须加载完整 `site.yaml`，将 site/manifest limits 取交集、rate 取最小值，并通过前置 receipts、双开关、人工 approval 和显式 `--live`。
- Gate 8由`joint-observe`同时记录A2 `joints-live`与PiPER JointState；动作只能来自现场已有approved单关节程序，人工逐关节表与`joint-validation` receipt是fault/shadow/live的前置。

## Source paths

- `policy_bundle/policy_manifest.yaml`
- `policy_bundle/parity/`
- `policy_bundle/metadata/`
- `src/a2_piper_stage2_deploy/`
- `ros2/a2_piper_stage2_direct/`
- `config/site.template.yaml`
- `scripts/stage2_gate.sh`
- `docs/operator_runbook_zh_CN.md`
- `docs/verified_stage2_contract.md`
- `docs/open_items.md`
