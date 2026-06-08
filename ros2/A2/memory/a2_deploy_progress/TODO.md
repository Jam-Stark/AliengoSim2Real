# TODO

## 2026-06-04 14:34 HKT

- [ ] 用 Unitree SDK2 sample 或实机 low-level smoke 对照 A2 CRC；如不一致，修正 `a2_crc` raw layout。
- [ ] 首次实机前增加或确认安全流程：

  - low-level control 前关闭 `ai_sport` / `ai_sports`；关闭和恢复内置 service 都已有 guarded MotionSwitcher script，但仍需 operator 按 `A2_REAL_ROBOT_TEST.md` 在实机执行和验证。
  - 日常 deployment command sequence 已有 `A2_REAL_DEPLOY_RUNBOOK.md`；operator 每次 real deployment 仍必须执行 runbook 中的 hardware emergency stop、one LowCmd publisher、`no-lowcmd`、MotionSwitcher release/restore 和 stop/restore/disconnect safety checks。
  - 离地或限功率 smoke。
  - 准备 hardware emergency stop。

## 2026-06-05 16:52 HKT

- [ ] 在部署机/实机验证 A2 R3 remote layout 和 safety gate：

  - 先运行 `A2/scripts/a2_real_robot_test.sh remote-live`，确认 raw/display `lx/rx/ry/ly` 和 pressed buttons 能实时随 stick/button 变化。
  - 再运行旧 `remote` summary validation 和 `a2_lowlevel_smoke --ros-args -p log_remote:=true`，确认 decode range 与 button names 一致。
  - `a2_policy_deploy --ros-args -p command_source:=remote` 在 PolicyActive 中不要求 `L2`；valid sticks 在 deadzone 后直接进入 policy command。
  - 不按 `L2` 时方向仍符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
  - `policy-enable-remote` 默认 `require_standup_before_policy=true`：first `A` 触发 stand-up interpolation，holder 持续发布 policy default pose，second `A` 在 `lx/rx/ly` deadzone 后为 zero 时进入 warmup/handover。
  - `PolicyWarmupHold` 持续发布 default stand pose，同时 history warm 到 `32` fresh frames；first policy action validation 通过后，下一 cycle 才进入 `PolicyActive` publish。
  - `Select` 是 primary local stop，`L2+B` 是附加 local stop path；local stop 能要求重新 two-A handover；`enable_motion=false` 下不发布 zero LowCmd，`enable_motion=true` 下发布 zero LowCmd。
  - stand-up / holder / warmup 阶段 `B` rising edge 能 cancel handover 并回到 `IdleBlocked`；`enable_motion=true` 下发布 zero LowCmd。

## 2026-06-05 19:49 HKT

- [ ] 按 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md` 在部署机 + real A2 上运行 connected validation，并回传 `/tmp/a2_real_robot_tests` logs：

  - 用默认 `A2_LOWSTATE_TOPIC=/lowstate`、`A2_LOWCMD_TOPIC=/lowcmd` 重新运行新版 `connected-preflight enp131s0`，确认 configured topic visibility/type check PASS。
  - 紧接着运行 `A2/scripts/a2_real_robot_test.sh no-lowcmd 5` observe-only；如果收到任何 configured `/lowcmd` message，先停止现有 LowCmd publisher，不进入任何 publish path。
  - `lowstate`
  - `joints-live` 人工逐关节 live table 验证；旧 `joints` 用作 run-end summary / CSV validation。
  - `remote-live` 人工 live raw/display sticks/buttons 验证；旧 `remote` 用作 summary validation。
  - `smoke-remote`
  - `motion-check enp131s0`，确认新版 MotionSwitcher helper compile log 中包含 SDK2 nested DDS include/lib dirs（如 `install/include/ddscxx` / `thirdparty/include/ddscxx` 和 `install/lib` / `thirdparty/lib/$(uname -m)`），`ldd` 优先解析到 SDK2 DDS libs，并成功打印 helper stage log / `CheckMode`；如仍 abort，回传阶段日志。
  - guarded `motion-release enp131s0`
  - guarded `zero-lowcmd`
  - `policy-listen-remote`
  - last-stage guarded `policy-enable-remote`，验证 first `A` stand-up、holder default pose、second `A` 在 sticks centered 后 warmup/handover、`Select` primary local stop、`L2+B` 附加 stop path 和 `B` cancel。
  - 测试结束停止 policy/LowCmd publisher，重新运行 `no-lowcmd 5` pass 后，用 guarded `motion-restore enp131s0` 或 Unitree App 恢复内置 motion service，并用 `motion-check enp131s0` 确认；新版 helper 会打印 raw `CheckMode form/name` 和 normalized `service`，其中 `form='0', name='ai', service='ai_sport'` 是 restore `ai_sport` 的 expected alias。

## 2026-06-05 20:10 HKT

- [ ] 在部署机/实机逐关节验证 A2 first-12 joint order 和 sign direction：

  - 优先运行 `A2/scripts/a2_real_robot_test.sh joints-live`，每次只移动一个 joint，确认 live table 中目标 label 的 `q`、`delta_from_start` 和 `range` 连续变化并被 `*` 标记。
  - 必要时再运行旧 `joints` 并设置 `A2_JOINT_CSV` 记录 time series，确认 `candidate_changed_joints` 与目标 label 一致。
  - 沿 sim/training convention 的 `+q` 方向移动，记录 `delta_from_start` sign 是否符合 no-inversion 假设。
  - 如发现 mapping mismatch 或 per-joint sign inversion 需求，先修正并复验，再进入任何 configured LowCmd topic（默认 `/lowcmd`）control path。

## 2026-06-08 22:21 HKT

- [ ] A2 brake gate 已改为 command override only，仍需部署机/实机验证阈值、符号和稳定性：

  - 用 `A2/scripts/a2_real_robot_test.sh policy-aux-live` 继续确认 listen-only / no-lowcmd boundary；虽然 wrapper 会传入 brake params，但 `enable_motion=false` 下不得发布 LowCmd。
  - 在 active `policy-enable-remote` 正常控制期间，用第二个 Docker terminal 运行 `A2/scripts/a2_real_robot_test.sh policy-aux-monitor 0` 订阅 `/a2/policy_aux`，观察 gate 前后的 `pred_base_force_local[0]`。
  - 验证默认 unitless threshold `pred_base_force_local[0] <= -0.6` 符号正确、2-step latch 后不出现 zero LowCmd stop、不切 stop mode、不清 PD，normal policy joint command 仍通过 `publish_joint_commands()` 持续发布。
  - 验证 brake active 后下一轮 policy observation command override 为 `[0,0,0]`，gait clock freeze/reset 为 standing phase `[0,1]`，且 release 判断基于 raw requested command；stick 回中 / command standing / not eligible / local stop 能 release。
  - 根据部署机/实机日志判断是否需要调整 `A2_POLICY_BRAKE_FORCE_X_THRESHOLD`、`A2_POLICY_BRAKE_HOLD_STEPS` 或 forward/yaw eligibility；不要把该 threshold 当 Newton。
