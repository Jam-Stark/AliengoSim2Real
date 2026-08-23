# A2 + PiPER Stage2 dual-policy 真实导出包

本包由 LMP authoritative Stage2 checkpoint `runner_state_020000.pt` 导出，供部署侧 AI 填入 `GeneralSim2Real/deploy/a2_piper_stage2`。它包含两个 deterministic TorchScript actor、完整 source contract、非平凡 reference parity 和离线验证器。

## 最短验证路径

```bash
python tools/validate_bundle.py .
```

期望结果：`status` 为 `pass`，dog input/output 为 `[1,1620] -> [1,12]`，arm input/output 为 `[1,600] -> [1,8]`，所有 parity error 不超过 `1e-6`。

## 关键部署语义

- 每 tick 先运行 arm actor；其输出 `[6:8]` 已在模型内部执行 `tanh`。
- `body_pitch/body_roll = clip(arm_output[6:8], -1, 1) * 0.4`。
- dog inference history 必须是 `committed_dog_history[54:1620] + same-state/new-plan preview frame`，不是覆盖已有 history 的最后一帧。
- dog/arm `last_action` 是上一策略 tick 的 raw actor output；不得写入 rate-limited hardware target。
- dog 与 arm actor 不使用 critic privileged signals，也不使用 base linear/angular velocity、TCP pose 或 arm velocity。
- `arm_j7/j8` 没有 actor output；训练时 target 固定为 0。arm actor 最后两维是 body plan，不是 gripper command。
- `runner_state_020000.pt` 的角色是 `training_state`，不是第三个在线 actor。

完整字段见 `policy_manifest.yaml`；reference key/shape 见 `metadata/lmp_source_contract.json`。

## 证据边界

模型、parity、动作 target 和 MuJoCo sim2sim 已验证；本包没有执行任何实机动作。A2/PiPER 的现场软限位、watchdog、hold/stop 和 bridge topic 必须继续通过部署仓库 Gate 验收。
