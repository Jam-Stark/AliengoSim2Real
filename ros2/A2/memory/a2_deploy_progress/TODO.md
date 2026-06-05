# TODO

## 2026-06-04 14:34 HKT

- [ ] 用 Unitree SDK2 sample 或实机 low-level smoke 对照 A2 CRC；如不一致，修正 `a2_crc` raw layout。
- [ ] 首次实机前增加或确认安全流程：

  - 关闭 `ai_sport` / `ai_sports`。
  - 离地或限功率 smoke。
  - 准备 hardware emergency stop。

## 2026-06-05 16:52 HKT

- [ ] 在部署机/实机验证 A2 R3 remote layout 和 safety gate：

  - `a2_lowlevel_smoke --ros-args -p log_remote:=true` 能随 stick/button 变化打印正确 `lx/rx/ry/ly` 和 button names。
  - `a2_policy_deploy --ros-args -p command_source:=remote` 中 `L2` release 强制 policy command `[0,0,0]`。
  - `L2` held 时方向符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
  - `Select` 和 `L2+B` 能触发 local stop、`publish_zero()`，并要求 history 重新 warm。

## 2026-06-05 19:49 HKT

- [ ] 按 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md` 在部署机 + real A2 上运行 connected validation，并回传 `/tmp/a2_real_robot_tests` logs：

  - 用默认 `A2_LOWSTATE_TOPIC=/lowstate`、`A2_LOWCMD_TOPIC=/lowcmd` 重新运行 `connected-preflight enp131s0`
  - `lowstate`
  - `joints`
  - `remote`
  - `smoke-remote`
  - `motion-check enp131s0`
  - guarded `motion-release enp131s0`
  - guarded `zero-lowcmd`
  - `policy-listen-remote`
  - last-stage guarded `policy-enable-remote`

## 2026-06-05 20:10 HKT

- [ ] 在部署机/实机逐关节验证 A2 first-12 joint order 和 sign direction：

  - 运行 `A2/scripts/a2_real_robot_test.sh joints`，必要时设置 `A2_JOINT_CSV` 记录 time series。
  - 每次只移动一个 joint，确认 `candidate_changed_joints` 与目标 label 一致。
  - 沿 sim/training convention 的 `+q` 方向移动，记录 `delta_from_start` sign 是否符合 no-inversion 假设。
  - 如发现 mapping mismatch 或 per-joint sign inversion 需求，先修正并复验，再进入任何 configured LowCmd topic（默认 `/lowcmd`）control path。
