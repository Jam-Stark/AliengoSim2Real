# PiPER Bridge Memory

Updated: 2026-08-25 HKT

本目录记录 A2 用户开发单元 PC2 作为 PiPER 原厂 USB-CAN hardware owner 的 ROS 2 deployment chain。它与 `../A2/` 的底盘链路并行：PC1 继续负责 A2，PC2 独占 PiPER CAN，笔记本通过同一 `192.168.123.0/24` DDS network 访问两者。

2026-08-24已获批完成PC2离线bootstrap、CAN activation与command-gate-closed bridge：`can_piper`为1 Mbit/s UP/ERROR-ACTIVE，PiPER joint state稳定50 Hz，diagnostics显示gate关闭、arm_status 0。21:05 HKT，现场操作员逐一人工移动六个PiPER关节，确认`arm_j1..arm_j6` position映射全部PASS；同场A2前12轴只读mapping也全部PASS。该记录不是direction/zero/limits/stop Gate。Ros-readonly Gate已PASS+APPROVED；未调用enable/resume或发布joint command。详情见`docs/PC2_READONLY_20260824.md`。

2026-08-25最终dual live已HARDWARE PASS：first-A自动resume/enable、同步init、second-A PolicyActive与两次显式14秒arm-goal轨迹均成功；evidence为Stage2 `evidence/live-both/20260825_004737_129916`。旧second-L2+B立即quick-stop PiPER，因此PiPER停在manifest reset/init位，没有回first-A启动休息位。新policy-host image `a2-piper-stage2:manual-arm-rest-stop-20260825`已build：默认PolicyActive保持PiPER init，显式arm-goal/trajectory才发布actor target；结束时先回first-A锁存休息位再stop。新结束行为尚待下一次实机验证，PC2 bridge image无需修改。

本轮收尾后PC2 bridge container已停止，Compose仍为`restart: no`；A2已恢复`ai_sport`。下次固定登录链路为m45使用`/home/baoquanc/.ssh/id_ed25519_pc2_stage2`连接`unitree@192.168.123.162`，PC2根目录`/home/unitree/Workspace/baoquanc`。PC2重启后按USB path `1-6:1.0`恢复`can_piper` 1 Mbit/s，再执行`runtime/bridge_ctl.sh start`；bridge启动保持gate closed，不自动enable/resume。

22:18 HKT按krushell fork的`piper_set_mit.py`定向切换到`MotionCtrl_2(1,1,0,0xAD)` + `JointCtrl`。这是MOVE J MIT/high-follow位置控制，不是`MotionCtrl_2(1,4,0,0xAD)` + `JointMitCtrl`逐电机阻抗控制。PC2候选image `a2-piper-pc2-bridge:mit-high-follow-20260824`已运行；diagnostics新增`joint_control_mode=move_j_mit_high_follow`与精确mode tuple，command gate仍关闭，全局`/piper/joint_command`和A2`/lowcmd`均无消息。Evidence：Stage2 session内`evidence/mit-high-follow/20260824_221815`。

A2侧此前约1000 Hz bare-DDS `/lowcmd`已实测确认为宇树`ai_sport`并由MotionSwitcher受保护release；release后mode为空、5秒计数为0。该交接没有修改PC2：PiPER bridge仍运行且command gate关闭，未调用enable/resume/stop、未发布joint command。A2测试结束的官方mode恢复以`../A2/MEMORY.md`和Stage2 Runbook为准。

Stage2 two-stage normal stop candidate中，第一次L2+B保持PiPER gate打开并用250 tick将PiPER平滑送回manifest reset pose，随后持续hold；A可重新warmup/resume。只有reset hold后的第二次L2+B才调用`/piper/stop`并结束测试。该candidate仅完成build和isolated no-output，PC2仍未执行enable/resume/stop或动作验证。

22:43 HKT，PiPER standalone baseline定向image `a2-piper-stage2:piper-roundtrip-baseline-20260824`已build并完成实时只读预检：当前state约`[-0.03698,-0.04747,0.05606,0.02255,0.31981,-0.13570] rad`，diagnostics为`move_j_mit_high_follow`、`MotionCtrl_2(1,1,0,0xAD)`、`command_gate_open=false`，未发送command。计划动作是5秒到`[0,1.48,-0.63,-0.84,0,1.57] rad`、保持5秒、5秒回到启动实测位姿，然后stop；不调用resume。Evidence：Stage2 session内`evidence/piper-roundtrip-preflight/20260824_224332`。客户端已兼容Humble将`DiagnosticStatus.level`提供为单字节值的情况。

Stage2 pre-policy init现使用bridge diagnostics协调首次target：first A后policy host持续发布实测PiPER position hold，gate关闭时bridge按既有contract丢弃；操作员显式enable后，下一条fresh hold满足0.20秒watchdog。direct node只有在fresh diagnostics确认`command_gate_open=true`后才开始从实测position到manifest init `[0.0,1.48,-0.63,-0.84,0.0,1.57] rad`的300-tick smoothstep。该代码尚未构建进PC2/policy host image，也未实机执行enable。

23:19 HKT更新：PiPER baseline已实机PASS并人工批准，evidence为Stage2 session内`evidence/piper-baseline/20260824_231903_98613`。bridge实际执行了resume、enable、MOVE J MIT/high-follow mode-ready handover、10秒前伸、5秒hold、10秒回程和stop；target最大误差`0.478 deg`，return最大误差`2.743 deg`。controller limit查询反馈六轴当前flash范围均为`[-180,180] deg`、最大速度均为`0.300 rad/s`，但部署position limit不得把该宽泛flash值当作机械范围：有效PiPER position边界继续取官方SDK/description范围与现场值交集；50 Hz live target rate按实测速度上限不超过`0.006 rad/tick`。

23:48 HKT更新：完成版Stage2 site已把PiPER position边界设为官方机械范围与controller反馈交集，并把六轴target rate统一设为`0.006 rad/20 ms`。最终policy-host image `a2-piper-stage2:dual-goal-pitch-trajectory-20260824`加载task-space goal `[0.4,1.0472,0] → [0.4,-1.2566,0] / 6 s`；goal pitch不是PiPER关节角，实际J1–J6仍由上述site边界限制。canonical live-both isolated preflight PASS，dummy topics下A2/PiPER output均未发布；evidence为Stage2 session内`evidence/live-preflight-both/20260824_234854_107096`。PC2 bridge仍处于manual_stop/gate closed，真正dual live尚未enable/resume。

Owner要求最终live流程不再手工分离diagnostics/resume/enable。候选`a2-piper-stage2:dual-triggered-trajectory-20260824`改为first A锁存实测位置后自动且仅一次`/piper/resume → /piper/enable`，gate open后才做init；second A进入PolicyActive但保持goal起点；显式`stage2_gate.sh trajectory`才开始6秒轨迹。隔离验证使用dummy PiPER services/topics，PC2实机状态未被此次验证改变；Stage2 evidence为`evidence/triggered-trajectory-preflight/20260825_000056_112019`。

2026-08-25 first-A尝试中PiPER resume/enable成功，但旧policy-host node未进入init，操作员在second A前退出；残留container随后由正式stop清理，PC2回到gate closed/ctrl_mode 0。真实graph证明PC2 command subscriber与原Stage2 publisher均为BEST_EFFORT，最初QoS不兼容推测错误。最终policy-host image `a2-piper-stage2:dual-triggered-handoverfix-20260825`以enable成功响应seed fresh gate-open，PC2 bridge未改；无输出graph/site PASS evidence为Stage2 `evidence/handover-fix-preflight/20260825_000936_116144`。

00:12 HKT第二次first-A中，PC2 resume/enable和handover seed成功，但约263 ms没有收到fresh command后bridge watchdog关闭gate；A2也未进入init。根因位于policy host：single-threaded executor被约1 kHz A2 LowState与PiPER callbacks占用，50 Hz control timer未及时运行，不是PC2 QoS、MIT/high-follow mode或A2 `ai_sport`。formal stop PASS evidence为Stage2 `evidence/stop/20260825_001300_117022`，PC2回到安全gate closed；00:19 HKT policy host只读复核mode为空、5秒`/lowcmd`计数0。policy-host image `a2-piper-stage2:dual-triggered-executorfix-20260825`改用独立control callback group与2-thread executor；真实state输入、command-remapped 8秒shadow PASS evidence为`evidence/executor-shadow-preflight/20260825_001628_118549`，PC2 bridge/image无需修改。

00:20 HKT第三次live中A2 first-A init成功，证明executor修复有效；PiPER仍quick-stop。m45与PC2时钟实测相差约36.5秒，对齐bridge log后真正fault为enable返回后约0.15秒的`arm_status=5`，官方SDK含义是joint communication abnormal，不是command timeout。当前只读`0x2A1#0000010000000000`显示arm status及err_code已恢复0。原`prepare_joint_control`只要求3个200 Hz正常样本，约15 ms；PC2新image `a2-piper-pc2-bridge:stable-enable-20260825`收紧为连续0.5秒`arm_status=0 && ctrl_mode=1`才放行，运行期任何非零status仍立即quick-stop。command-gate-closed重启后diagnostics为arm_status/ctrl_mode=`0/0`、joint/status 200 Hz、hardware_stop_required false。

同次second A暴露A2 actor首帧target接管跳变。Policy-host新image `a2-piper-stage2:dual-roundtrip-handover-20260825`在PolicyActive前50 tick平滑接管两路actor output，并把task-space流程改为保持init `[0.6,0,0]`，trigger后4秒到`[0.4,1.0472,0]`、6秒到`[0.4,-1.2566,0]`、4秒回init。真实state、command-remapped no-output PASS evidence为Stage2 `evidence/roundtrip-handover-preflight/20260825_002957_122308`。

00:36 HKT第四次first-A中PC2 stable-enable返回成功，但Stage2未进入init，约276 ms后PC2记录command timeout；formal stop PASS。stable-enable service在PC2单线程executor中执行约1秒，期间暂停`/piper/joint_states`，使policy host在enable future完成前按普通200 ms stale-state路径清掉first-A lifecycle。PC2 bridge无需回退稳定窗口；policy-host image `a2-piper-stage2:handover-stale-fix-20260825`只在enable in-flight handover phase保持锁存hold并容许该预期state空窗，正常运行期stale fault不变。独立A2 50 Hz state + 单线程dummy PiPER 0.70秒阻塞fixture PASS evidence为Stage2 `evidence/handover-stale-preflight/20260825_004327_128268`。

## Entries

- `memory/piper_pc2_bridge/`
  - `piper_bridge` package、ROS 2 interface、PC2 Docker、SocketCAN、watchdog、read-only/motion validation，以及 `krushell/piper_sdk` manipulation adapter。

## Routing

- 修改 `ros2/Piper/**`、`/piper/joint_states`、`/piper/joint_command`、PiPER enable/stop/disable services、PC2 USB-CAN ownership、remote manipulation runner 时，先读 `memory/piper_pc2_bridge/description.md`。
- 需要判断实机 blocker 和下一步时，再读同 entry 的 `TODO.md` 与 `DONE.md`。
- A2 low-level `/lowstate`、`/lowcmd`、MotionSwitcher 和底盘 policy 事实仍以 `../A2/MEMORY.md` 为准。
- PiPER hardware/manual 与 SDK reference 以随附资料和官方仓库为准，不复制长文档到 memory。
- 全局 routing 入口是 `../../MEMORY.md`。
