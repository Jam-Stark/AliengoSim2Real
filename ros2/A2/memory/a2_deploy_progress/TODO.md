# TODO

## 2026-06-04 14:34 HKT

- [ ] 用 Unitree SDK2 sample 或实机 low-level smoke 对照 A2 CRC；如不一致，修正 `a2_crc` raw layout。
- [ ] 首次实机前增加或确认安全流程：

  - 关闭 `ai_sport` / `ai_sports`。
  - 离地或限功率 smoke。
  - 准备 hardware emergency stop。

## 2026-06-05 16:52 HKT

- [ ] 在部署机/实机验证 A2 R3 remote layout 和 safety gate：

  - 先运行 `A2/scripts/a2_real_robot_test.sh remote-live`，确认 raw/display `lx/rx/ry/ly` 和 pressed buttons 能实时随 stick/button 变化。
  - 再运行旧 `remote` summary validation 和 `a2_lowlevel_smoke --ros-args -p log_remote:=true`，确认 decode range 与 button names 一致。
  - `a2_policy_deploy --ros-args -p command_source:=remote` 中 `L2` release 强制 policy command `[0,0,0]`。
  - `L2` held 时方向符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
  - `Select` 和 `L2+B` 能触发 local stop 并要求 history 重新 warm；`enable_motion=false` 下不发布 zero LowCmd，`enable_motion=true` 下发布 zero LowCmd。

## 2026-06-05 19:49 HKT

- [ ] 按 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md` 在部署机 + real A2 上运行 connected validation，并回传 `/tmp/a2_real_robot_tests` logs：

  - 用默认 `A2_LOWSTATE_TOPIC=/lowstate`、`A2_LOWCMD_TOPIC=/lowcmd` 重新运行新版 `connected-preflight enp131s0`，确认 configured topic visibility/type check PASS。
  - 紧接着运行 `A2/scripts/a2_real_robot_test.sh no-lowcmd 5` observe-only；如果收到任何 configured `/lowcmd` message，先停止现有 LowCmd publisher，不进入任何 publish path。
  - `lowstate`
  - `joints-live` 人工逐关节 live table 验证；旧 `joints` 用作 run-end summary / CSV validation。
  - `remote-live` 人工 live raw/display sticks/buttons 验证；旧 `remote` 用作 summary validation。
  - `smoke-remote`
  - `motion-check enp131s0`
  - guarded `motion-release enp131s0`
  - guarded `zero-lowcmd`
  - `policy-listen-remote`
  - last-stage guarded `policy-enable-remote`

## 2026-06-05 20:10 HKT

- [ ] 在部署机/实机逐关节验证 A2 first-12 joint order 和 sign direction：

  - 优先运行 `A2/scripts/a2_real_robot_test.sh joints-live`，每次只移动一个 joint，确认 live table 中目标 label 的 `q`、`delta_from_start` 和 `range` 连续变化并被 `*` 标记。
  - 必要时再运行旧 `joints` 并设置 `A2_JOINT_CSV` 记录 time series，确认 `candidate_changed_joints` 与目标 label 一致。
  - 沿 sim/training convention 的 `+q` 方向移动，记录 `delta_from_start` sign 是否符合 no-inversion 假设。
  - 如发现 mapping mismatch 或 per-joint sign inversion 需求，先修正并复验，再进入任何 configured LowCmd topic（默认 `/lowcmd`）control path。
