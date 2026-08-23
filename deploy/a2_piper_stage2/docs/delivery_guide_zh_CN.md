# A2 + PiPER Stage2 dual-policy 交付指南

操作员请直接执行 [operator_runbook_zh_CN.md](operator_runbook_zh_CN.md)。该文件是唯一命令入口，覆盖Ubuntu/Docker bootstrap、offline parity、network/read-only、两套既有baseline、direct dry-run、Gate 8只读joint observer与人工逐关节验收、fault、十分钟shadow、dog-only、arm-only和both live，并为每步规定精确PASS、失败停止与evidence路径。

## 交付架构

正式实机路径为C++ `a2_piper_stage2_direct`：

- 与main成功locomotion部署相同，进程内组合`A2LowLevelInterface`；
- 读取raw `/lowstate`，使用fixed training→A2 mapping；
- 只通过既有`publish_joint_commands()`进入`/lowcmd`；
- 订阅PC2 `/piper/joint_states`并发布named `/piper/joint_command`；
- PC2继续是PiPER CAN唯一owner。

所以A2不缺semantic信息或成功control path；旧方案缺少的只是“供Python external host使用的named A2 endpoint”。本交付选择direct mode，不再把该endpoint作为live blocker。Python `ros-shadow/ros-live`保留为external-semantic unavailable的明确错误，不是实机入口。

## 已锁定policy contract

- dog actor：float32 `[B,1620] -> [B,12]`，`30×54` history；
- arm actor：float32 `[B,600] -> [B,8]`，`30×20` history；`[0:6]`控制PiPER，`[6:8]`是body plan；
- `50 Hz`，arm-first，dog使用same-state/new-plan preview；
- dog/arm `last_action`是上一tick raw actor output；
- target为`default + 0.25 × raw`；bundle `final_joint_target_rad`是site limit/rate limit前的nominal target；
- runner是`training_state`，gripper absent。

用户已确认dual policy完成sim2sim，末态arm position error约`0.039 m`；它是policy效果证据，不是deployment blocker。它也不替代A2/PiPER mapping、limit、watchdog、stop和hardware Gate。

## 证据分级

| 标记 | 含义 |
| --- | --- |
| `INDEPENDENTLY_VERIFIED` | bundle结构、TorchScript/NPZ与数值关系已独立复核 |
| `EXPORT_RECEIPT` | export metadata/handoff声明，不能替代目标host结果 |
| `REPOSITORY_CONTRACT` | 当前代码中的topic、mapping、PD与timeout事实 |
| `SITE_VERIFIED` | 本次session命令、观察与operator receipt共同确认 |
| `TO_VERIFY` | 仍缺现场evidence，live preflight会拒绝 |

## 最短入口

```bash
cd deploy/a2_piper_stage2
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only
./scripts/stage2_gate.sh init --operator <name>
./scripts/stage2_gate.sh offline
./scripts/stage2_gate.sh next
```

Live同时要求：

```text
config/site.yaml 不含 TO_VERIFY
safety.output_enabled == true（operator手动修改）
全部前序Gate有PASS与approval receipts
目标component有live approval receipt
STAGE2_ALLOW_LIVE == 1
命令行显式 --live
```

Gate runner不会修改site、不会打开`output_enabled`、不会调用PiPER resume。当前交付不声称任何尚未生成site receipt的实机Gate已通过。
