# Stage2 bring-up gates

Gate 必须按顺序完成。每个 Gate 的 acceptance evidence 要记录执行机、版本、配置、命令、时间、结果和现场批准人。后续 Gate 不能用前一 Gate 的“预期结果”代替实际记录。

所有operator命令、精确PASS、停止条件和evidence路径统一见 [operator_runbook_zh_CN.md](operator_runbook_zh_CN.md)。本表定义验收语义，不是已通过记录；实际状态只认 `.stage2_sessions/<id>/` receipts。

Direct C++ node已消除旧external-semantic A2 endpoint blocker：它复用同一台A2的`A2LowLevelInterface`、fixed mapping、PD与LowCmd boundary。当前ceiling由现场receipts决定，不能因代码存在就写成实机Gate通过。

| Gate | Allowed output | Acceptance focus | Current status |
| --- | --- | --- | --- |
| 0 | none | power、cables、support rig、physical E-stop、roles、test area | `TO_VERIFY` |
| 1 | none | lock LMP bundle/contract；区分独立 evidence 与 export receipt；列出 site unknowns | partial offline evidence；not accepted for hardware |
| 2 | mock only | container、manifest load、model/reference parity、manifest/reference parity、mock logs | executable offline；target-host result pending |
| 3 | real actors, no robot | target-host CPU parity、benchmark、long-running stability | export receipt exists；deployment-host pass pending |
| 4 | read-only | raw A2 `/lowstate` + PiPER semantic state、types、rates、joint names、receipt age/skew | executable；site result pending |
| 5 | existing PiPER program | low-speed/low-amplitude PiPER-only baseline | hardware pending |
| 6 | existing A2 program | A2 stand/hold/stop baseline | real acceptance pending |
| 7 | no output | direct node loads contract, parses synchronized state and runs both actors with `enable_motion=false` | executable dry-run；不是disabled-drive publish test |
| 8 | one joint, supported | direction、units、zero、limits、mapping，one joint at a time | read-only observer executable；approved single-joint program and human review pending |
| 9 | controlled fault | network loss、process stop、frozen state、deadline、local watchdog/recovery | process-stop automation exists；hardware scenarios pending |
| 10 | dual-policy shadow | at least 10 minutes；status age/skew/inference、no A2/PiPER command | executable；site receipt pending |
| 11 | one component live | dog-only then arm-only，low amplitude | gated command exists；not yet hardware accepted |
| 12 | coupled, supported/fixture | both、no-contact coupling、plan propagation、independent stop paths | gated command exists；not yet hardware accepted |
| 13 | free standing, no contact | gradually expand range/speed while preserving stop margin | manual future Gate |
| 14 | task contact | low speed/force、large clearance、task-specific limits | manual future Gate |

## Gate 0 — Physical and operational readiness

Required evidence：

- A2、PiPER、PC2、policy host、switch和USB-CAN连接图；
- A2稳定支撑与PiPER安全工作空间；
- hardware emergency stop实际可达且已由人员验证；
- operator、E-stop operator、observer职责；
- stop criteria和撤离区域；
- known-good A2/PiPER standalone recovery procedure。

没有这些现场条件，不运行任何 command publisher。

## Gate 1 — Source contract lock

必须冻结：

- bundle目录与 19-file inventory；
- `policy_manifest.yaml` schema/version；
- dog/arm actor shapes、history、joint order、action scale；
- nominal target parity scope；
- export receipt与独立 evidence的差异；
- 全部 unresolved lineage/site fields。

特别记录 `final_joint_target_rad` 是 pre-site-limit nominal target。Arm reference的 `arm_j5=1.4376565 rad` 超过 LMP range上限 `1.22 rad`，证明 limited target不能与该 reference直接相等。

## Gate 2 — Container and mock

```bash
./scripts/check_policy_host.sh
./scripts/build_container.sh
./scripts/run_shadow.sh mock
```

Acceptance：parity pass、manifest/reference pass、benchmark有记录、mock完成且 `hardware_output=false`。这不是 robot shadow。

## Gate 3 — Real actors without robot

在最终 policy host CPU image 上重复 Gate 2，并运行现场批准时长的 realtime/continuous mock。记录 p50/p95/p99/max、deadline margin、non-finite检查和 process stability。Export host benchmark只能作为参考。

## Gate 4 — Read-only semantic state

Direct mode只读检查：

- A2 raw `/lowstate` type/rate与PiPER semantic topic/type/QoS/rate；
- fixed A2 training→raw mapping以及PiPER exact joint names/order；
- raw A2 `wxyz` attitude与Stage2 projected-gravity parity；
- source/receipt timestamp、age与cross-component skew；
- no active command publisher；
- snapshot assembly不猜测、不补零。

这里明确使用raw `/lowstate`，因为policy与low-level adapter在同一C++进程；禁止由Python external policy host另行构造 `/lowcmd`。

## Gates 5–6 — Existing standalone baselines

Stage2 runtime不发布。分别用已经验证的 PiPER与A2程序确认低幅动作、hold、stop、recovery。任何 standalone baseline失败都应先修复原控制域，不能在dual policy层增加fallback。

## Gate 7 — Disabled-drive command parsing

当前实现只验证`enable_motion=false`时双actor完成一次ready tick，并由existing A2 observer与PiPER topic observer确认两路都无command。它确认contract/parse/infer，不声称drives-disabled command publish已跑过。PiPER命令仍必须是explicit names、absolute radians、one point；plan `[6:8]`不进入PiPER command，gripper保持absent。

## Gate 8 — Supported one-joint validation

P1先运行只读observer：

```bash
./scripts/stage2_gate.sh joint-observe --duration 600
```

该命令只同时采集A2 existing `joints-live`与`/piper/joint_states`，不发布A2/PiPER command。P2/现场负责人只能使用各控制域已有且已批准的单关节程序；一次一个关节，逐项记录command name、bridge/raw index、实际运动方向、zero、unit、允许range和stop。由于site limits尚未知，Gate runner不会构造或猜测任何target。没有approved single-joint program时本Gate停止。

全部关节的`joint_validation_table.tsv`逐行填完并由现场负责人审阅后，记录；脚本会拒绝18个关节中任一必填验收列为空的表：

```bash
./scripts/stage2_gate.sh approve --gate joint-validation --operator <你的名字>
```

该approval是Gate 9的硬前置。禁止一次enable全部关节“看起来是否正确”，也禁止只凭observer PASS跳过人工逐关节验收。

## Gate 9 — Fault behavior

受控验证：

- policy host network断开；
- policy process停止；
- A2或PiPER状态冻结/超龄；
- message skew超阈值；
- inference deadline连续miss；
- PC2 command/feedback timeout；
- restart与explicit recovery。

预期行为必须来自现场认可的hold/stop contract；不得在没有证据时预填。

## Gate 10 — Ten-minute coupled shadow

`enable_motion=false`与`live_acknowledged=false`，两路命令输出保持禁用。至少记录：

- state age/skew；
- inference/deadline；
- status持续`contract=verified;mode=shadow;state=ready`；
- A2 `no-lowcmd`与PiPER no-command observers；
- blocked/reason与日志完整性。

## Gates 11–14 — Progressive live

每级需要前级签字和重新确认physical E-stop：

- Gate 11：dog-only、arm-only分别低幅；arm-only仍向A2发布default-position PD hold；
- Gate 12：支撑/夹具，无任务接触；
- Gate 13：自由站立、无接触，逐步扩大范围；
- Gate 14：任务接触，从低速、低力和大安全间距开始。

任一异常返回最近已通过的 no-motion/read-only Gate，不自动恢复 live。
