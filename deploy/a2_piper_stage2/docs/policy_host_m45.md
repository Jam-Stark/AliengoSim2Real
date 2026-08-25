# m45 policy host 实机档案

最后更新：2026-08-25 HKT

本页只记录已在 `baoquanc@ai-precog-m45` 上实际采集或执行的事实。它是 [operator_runbook_zh_CN.md](operator_runbook_zh_CN.md) 的主机侧 companion，不代表任何 A2/PiPER hardware Gate 已通过。

## 已采集主机事实

| 项目 | m45 实测值 |
| --- | --- |
| SSH alias | `m45`，免密码登录可用 |
| hostname / user | `ai-precog-m45` / `baoquanc` |
| repository | `/home/baoquanc/Workspace/GeneralSim2Real` |
| branch | `codex/a2-piper-lmp-stage2-deploy-20260823` |
| host OS | Ubuntu 24.04.4 LTS，x86_64 |
| kernel | `7.0.0-30-generic` |
| memory / swap | 30 GiB / 8 GiB |
| root filesystem | 1.9 TiB，总可用约 1.8 TiB（采集时） |
| GPU | NVIDIA GeForce RTX 5070 Ti，16303 MiB |
| NVIDIA driver | `595.84`，host `nvidia-smi` 正常 |
| robot Ethernet | `enp130s0 / 192.168.123.222/24`；接线后carrier与地址已恢复 |
| management/Wi-Fi | `wlp129s0`，`10.13.179.248/22` |
| ROS domain | `0`（policy 配置值；待 A2/PC2 接入后共同复核） |

## 当前接受的部署解释

- Host 使用 Ubuntu 24.04；container 仍是已选定的 Ubuntu 22.04 + ROS 2 Humble A2 base image。Host 不需要原生安装 ROS 2 Humble。
- Docker Engine 使用 Docker 官方 Noble apt repository。Docker 官方当前将 Ubuntu Noble 24.04 LTS列为支持版本。
- Stage2 direct C++ runtime 当前只接受 CPU LibTorch 2.7.0 路径。5070 Ti 与 host driver 已记录，但本轮不因此临时切换 CUDA artifact，也不安装 NVIDIA Container Toolkit；这避免改变已经完成 parity 的 runtime contract。
- `enp130s0` 是 robot Ethernet；不要把 Wi-Fi `wlp129s0` 传给 DDS/Stage2 配置。A2/PC2 接线后按 Runbook 显式恢复 `192.168.123.222/24` 并运行 Network Gate。

## 执行状态

| 项目 | 状态 | 证据/停止条件 |
| --- | --- | --- |
| SSH 与 repo 路径 | PASS | 已从控制机免密码登录并读取 branch/worktree |
| OS/architecture | PASS | Ubuntu 24.04.4 LTS / x86_64 |
| NVIDIA host driver | PASS | RTX 5070 Ti、driver 595.84 可由 `nvidia-smi` 读取 |
| robot NIC/IP | PASS | `enp130s0 / 192.168.123.222/24`，PC1 `.123.161` 与PC2 `.123.162`均可ping |
| Docker Engine/Compose | PASS | Engine `29.7.2`、Compose `v5.5.0`、overlayfs、非 sudo daemon access 与 hello-world 均通过 |
| `docker/.env` mock policy 配置 | PASS | 已写入 `enp130s0`、`192.168.123.222/24`、domain `0`、真实 bundle、mock site 与 CPU Torch/LibTorch 2.7 路径 |
| Stage2 session | LIVE-BOTH APPROVED / FIRST-A RETRY READY | session `20260824_173406`；baseline、dry-run、joint-observe、fault、shadow、isolated live preflight及live-both approval均完成；前两次first-A问题已定位并修复，等待现场重试init |
| Stage2 image / offline Gate | PASS | image `a2-piper-stage2:humble-torch2.7.0-cpu`；target C++ build、parity、benchmark、500 tick mock shadow 全部通过 |
| Pre-enable init candidate | BUILD PASS / ISOLATED NO-OUTPUT PASS | 独立tag `a2-piper-stage2:preenable-init-20260824`；新版direct binary shadow ready、diagnostics订阅存在，隔离A2/PiPER command均零消息；未覆盖offline PASS image |
| Two-stage stop candidate | BUILD PASS / ISOLATED NO-OUTPUT PASS | tag `a2-piper-stage2:preenable-stop-lifecycle-20260824`；第一次L2+B回reset hold、A恢复policy、第二次L2+B停止PiPER并让A2趴地；动作尚未实机执行 |
| PiPER MIT/high-follow candidate | BUILD PASS / COMMAND-GATE-CLOSED NO-OUTPUT PASS | Stage2 tag `a2-piper-stage2:mit-high-follow-20260824`；PC2 tag `a2-piper-pc2-bridge:mit-high-follow-20260824`当前运行，diagnostics确认`MotionCtrl_2(1,1,0,0xAD)`、gate closed，A2/PiPER command均无消息 |
| PiPER round-trip baseline | PASS / APPROVED | 最终tag `a2-piper-stage2:piper-baseline-return-tolerance-20260824`；10秒到init target、保持5秒、10秒回实测启动位姿并stop；target/return最大误差`0.478/2.743 deg`，evidence `piper-baseline/20260824_231903_98613` |
| Coupled dry-run | PASS / APPROVED | 60秒真实state dual shadow ready、隔离A2 command与PiPER command均无输出；evidence `dry-run/20260824_232605_100005` |
| Joint observe / process fault / coupled shadow | PASS | evidence分别为`joint-observe/20260824_233131_101388`、`fault/20260824_233346_101892`、`shadow/20260824_233514_102361`；Owner本session跳过正式joint-validation表和shadow receipt |
| Live-both isolated preflight | PASS / NO OUTPUT | 最终tag `a2-piper-stage2:dual-goal-pitch-trajectory-20260824`；canonical status确认task-space goal `[0.4,1.0472,0] → [0.4,-1.2566,0] / 6 s`已加载，dummy topics下A2/PiPER output均not published；evidence `live-preflight-both/20260824_234854_107096` |
| Triggered trajectory candidate | BUILD PASS / ISOLATED NO-OUTPUT PASS | tag `a2-piper-stage2:dual-triggered-trajectory-20260824`；first A自动单次resume/enable，second A进入PolicyActive并保持起点，`stage2_gate.sh trajectory`显式启动轨迹；隔离状态`goal_trajectory_state=armed`且错误phase触发被拒绝，evidence `triggered-trajectory-preflight/20260825_000056_112019` |
| First-A handover fix | BUILD PASS / ISOLATED NO-OUTPUT PASS | 首次实机resume/enable成功但未进入init；残留container已双路径stop。最终tag `a2-piper-stage2:dual-triggered-handoverfix-20260825`以enable成功响应seed fresh gate-open，双方command QoS实测均BEST_EFFORT，Ctrl+C自动stop；evidence `handover-fix-preflight/20260825_000936_116144` |
| First-A executor fix | BUILD PASS / REAL-STATE ISOLATED NO-OUTPUT PASS | 第二次实机handover seed成功但single-threaded executor使50 Hz control timer被约1 kHz state callbacks饿死，263 ms后PiPER watchdog关gate；tag `a2-piper-stage2:dual-triggered-executorfix-20260825`使用独立callback group和2-thread executor，8秒真实state负载收到35条status且隔离输出为零；evidence `executor-shadow-preflight/20260825_001628_118549` |
| Third live / round-trip handover fix | LIVE SAFELY ENDED / NEW IMAGE NO-OUTPUT PASS | first-A A2 init成功；second-A出现actor接管跳变，PiPER因瞬态`arm_status=5`quick-stop；两段L2+B与formal stop完成。新tag `a2-piper-stage2:dual-roundtrip-handover-20260825`增加1秒output handover及`init→start→end→init` 4/6/4秒轨迹；真实state shadow 36条ready status、两路隔离command零字节，evidence `roundtrip-handover-preflight/20260825_002957_122308` |
| Stable-enable state-gap fix | BUILD PASS / ISOLATED DELAYED-ENABLE PASS | 第四次first-A因PC2 enable callback暂停state >200 ms而丢失lifecycle，formal stop PASS。tag `a2-piper-stage2:handover-stale-fix-20260825`仅在enable in-flight阶段保持锁存hold；0.70秒dummy PiPER空窗后约18 ms进入StandUpInterpolating，fixture输出仅在隔离topics；evidence `handover-stale-preflight/20260825_004327_128268` |
| Final dual live | HARDWARE PASS / SAFELY ENDED | tag `a2-piper-stage2:handover-stale-fix-20260825`；first-A同步init、second-A PolicyActive、两次显式14秒轨迹与两段L2+B完成；live `20260825_004737_129916`，formal stop `20260825_005119_130865` |
| Manual-arm/rest-stop next image | BUILD PASS / HARDWARE PENDING | tag `a2-piper-stage2:manual-arm-rest-stop-20260825`；PolicyActive默认零arm task command并保持PiPER init，显式`arm-goal`/`trajectory`才发布actor target；第二次L2+B回first-A启动休息位后再stop |
| A2 read-only | PASS | domain 0可见`/lowstate`与`/lowcmd`精确类型；`/lowstate`约1052.7 Hz |
| A2官方控制权交接 / baseline | PASS / APPROVED / RESTORED | ReleaseMode后`/lowcmd`停止；最终测试结束后`no-lowcmd=0`并恢复`service='ai_sport'`，evidence `a2-restore/20260825_005440_131866`；每次live仍重新执行`motion-check → motion-release` |
| PC2 CAN/bridge read-only | PASS / BRIDGE STOPPED | `can_piper`配置为1 Mbit/s；bridge image `a2-piper-pc2-bridge:stable-enable-20260825`不变，本轮收尾后container停止，Compose `restart: no` |
| A2/PiPER joint mapping预观察 | PASS（操作员确认，非Gate receipt） | 人工逐轴移动时，A2前12轴raw index/label与PiPER `arm_j1..arm_j6` position响应全部对应；没有命令输出 |
| Network/ROS/hardware Gates | LIVE-BOTH APPROVED | operator approval已记录；每次实机启动仍要求`STAGE2_ALLOW_LIVE=1`和显式`--live` |

## m45 精确命令

Docker bootstrap 已完成，以下命令无需再运行，仅保留为重装恢复记录：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2

./scripts/bootstrap_policy_host_ubuntu.sh
```

当前复核命令：

```bash
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only

./scripts/stage2_gate.sh status
```

当前`offline`、`network`、`physical`和`ros-readonly`均已由操作员批准。当前next是A2 standalone baseline；不能预先批准后续motion/live Gate。

`stage2_gate.sh next`现在打印guarded A2 baseline命令。此前active bare-DDS `/lowcmd`已通过MotionSwitcher实测确认为官方`ai_sport`：release前约1000 Hz，`ReleaseMode ret=0`且mode为空后5秒计数为0。AI不代签人工receipt。

2026-08-24 21:47 HKT只读采集当前A2趴地姿态5秒，5263个有效样本且每轴range不超过`0.0001 rad`；training-order目标已写入`config/stage2_direct.params.yaml`。最终two-stage stop candidate build与隔离无输出PASS evidence为：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-stop-lifecycle/20260824_220228
```

2026-08-24 21:05 HKT，操作员使用A2 `joints-live`低刷新率表与PiPER `/piper/joint_states`，在command gate关闭、无A2 LowCmd、无PiPER joint command的条件下逐轴人工移动，确认两套关节映射全部PASS。该事实只证明实时state的joint identity/order/index响应；Gate 8仍需完整的direction/unit/zero/limits/stop人工表和receipt。

Network PASS evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/network/20260824_202236_44746
```

PC2 offline staging evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pc2-bootstrap/20260824_210000
```

PC2 bridge rebuild与ros-readonly evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pc2-bridge-rebuild/20260824_typing_extensions_fix
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/ros-readonly/20260824_205245_59854
```

Pre-enable candidate build与no-output evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-init/20260824_212010
```

A2官方`ai_sport` release与`/lowcmd`停止evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/a2-motion-release/20260824_213519
```

测试结束并确认A2安全落地、Stage2 live container停止、`no-lowcmd 5` PASS后，使用`STAGE2_ALLOW_A2_RESTORE=1 ./scripts/stage2_gate.sh restore-a2 --iface enp130s0 --operator baoquanc`恢复官方`ai_sport`；验收输出必须回到`form='0', name='ai', service='ai_sport'`。

## m45 offline 实测结果

最终 PASS evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/offline/20260824_192059_33754
```

- Python Torch：`2.7.0+cpu`；dog/arm shape 与全部 reference parity 误差不超过 `1e-6`。
- 200 warmup + 2000 measured arm/dog pairs：mean `0.252054 ms`、p50 `0.251084 ms`、p95 `0.260804 ms`、p99 `0.265491 ms`、max `0.366795 ms`；policy period `20 ms`。
- 500 tick mock shadow：mean `0.310556 ms`、p95 `0.314459 ms`、max `3.278607 ms`，`hardware_output=false`。
- Target image 中 Python Torch import 与 direct executable dynamic libraries 均再次只读确认通过。
- Post-offline host probe：`evidence/post_offline_20260824/policy_host_read_only.log`。
- Connected host/A2 probe：`evidence/connected_a2_pc2_20260824/`与`evidence/a2_readonly_20260824/`。

2026-08-24 23:48 HKT，m45定向重建dual goal trajectory image成功。manifest的task-space pitch训练采样范围是`[-1,1] rad`，终点`-1.2566 rad`超出采样范围但不是PiPER关节角；按训练端给出的试验目标保留。direct status新增goal、arm actor body pitch/roll plan和A2实测roll/pitch，实际J1–J6 target继续受完成版site机械范围及`0.006 rad/tick`约束。真正live未启动。

## 目标机首次构建暴露并修复的问题

1. `a2_lowlevel` 的 CMake export set 原先混入两个 executable，导致下游 direct package 尝试链接 executable target；现只 export library。
2. Ubuntu Jammy 的 pip 22 将 PEP 621 package 安装为 `UNKNOWN`; image 固定使用目标机验证过的 pip 25.1.1。
3. Base C++ LibTorch 2.1、Stage2 C++ LibTorch 2.7 与 Python Torch wheel 共用 `LD_LIBRARY_PATH` 会令 Python import 崩溃；现在 Python不再被外部 C++ LibTorch shadow，C++ executable通过 install RPATH解析Stage2 LibTorch。
