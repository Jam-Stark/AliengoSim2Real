---
name: stage2_dual_policy
scope: deploy/a2_piper_stage2
status: dual-live-pass-shutdown-complete-next-runtime-built
last_updated: "2026-08-25 00:55 HKT"
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

- Session `20260824_173406` 的dual live已HARDWARE PASS：first-A同步init、second-A PolicyActive、两次显式arm轨迹与two-stage stop均完成；evidence为`evidence/live-both/20260825_004737_129916`与`evidence/stop/20260825_005119_130865`。
- 该run证明旧second-L2+B只让A2趴地，PiPER在reset位立即stop。新build `a2-piper-stage2:manual-arm-rest-stop-20260825`将PiPER同步送回first-A锁存的启动休息位后再stop；该新行为尚未hardware验证。
- 新PolicyActive默认行为是PiPER保持manifest init，arm task command为零；只有`/a2_piper_stage2/arm_goal`或trajectory service才发布arm actor target。Base locomotion command继续来自A2 remote。PC2 bridge v1仍无`arm_j7/j8`gripper interface。
- 2026-08-25收尾已恢复A2 `ai_sport`并停止PC2 bridge；恢复evidence为`evidence/a2-restore/20260825_005440_131866`。

- `ros2/Piper` 的 `/piper/joint_states`、`/piper/joint_command`、`arm_j1..arm_j6` absolute-radian interface 与 actor control维度对齐，但 hardware status 仍是 pending。
- 正式 C++ node 组合 `A2LowLevelInterface`，复用 raw `/lowstate`/`/lowcmd`、training mapping、PD、mode 和 CRC boundary；不再需要 laptop-facing named A2 semantic bridge。
- Python external-semantic transport 只作明确的 unavailable 边界；offline parity/runtime 仍是 oracle，实机 shadow/live 只走 `a2_piper_stage2_direct`。
- A2 raw IMU quaternion 是 `wxyz`；direct runtime 按同样顺序计算 Stage2 gravity/roll-pitch，仍需现场 read-only parity。
- Live startup 必须加载完整 `site.yaml`，将 site/manifest limits 取交集、rate 取最小值，并通过前置 receipts、双开关、人工 approval 和显式 `--live`。
- Gate 8由`joint-observe`同时记录A2 `joints-live`与PiPER JointState；动作只能来自现场已有approved单关节程序，人工逐关节表与`joint-validation` receipt是fault/shadow/live的前置。

## Current policy host: m45

- 正式 host：`baoquanc@ai-precog-m45`，repo `/home/baoquanc/Workspace/GeneralSim2Real`，branch `codex/a2-piper-lmp-stage2-deploy-20260823`。
- 实测 Ubuntu 24.04.4 LTS / x86_64、kernel `7.0.0-30-generic`、30 GiB RAM、约 1.8 TiB available disk。
- RTX 5070 Ti 16303 MiB、driver 595.84 正常；Stage2 仍走已验证 CPU LibTorch 2.7，不把 GPU存在等同于 CUDA runtime 已验证。
- robot NIC是`enp130s0 / 192.168.123.222/24`；接线后可同时ping PC1`.123.161`与PC2`.123.162`。Wi-Fi`wlp129s0`是管理网络，不能传给DDS runtime。
- `docker/.env` 已配置真实 bundle + mock site + domain 0；session `20260824_173406` 已初始化。
- Docker Engine 29.7.2、Compose v5.5.0、overlayfs、daemon access与hello-world PASS；CPU Stage2 image已在m45构建。
- Session `20260824_173406` offline Gate PASS：全部 parity `<=1e-6`；2000 pair benchmark mean/p95/p99/max为`0.252/0.261/0.265/0.367 ms`；500 tick mock shadow PASS且无hardware output。
- Target build修正了A2 library/export set、Jammy pip 22 PEP 621 metadata以及C++ LibTorch与Python wheel的runtime library shadow；最终Python Torch import与direct executable动态库均通过。
- M45 domain 0 A2-only read-only probe PASS：`/lowstate`与`/lowcmd`类型正确，lowstate约1052.7 Hz。
- PC2已完成Docker29.7.2、Compose5.5.0、can-utils、SDK source、1 Mbit/s`can_piper`与command-gate-closed bridge；PiPER joint state稳定50 Hz。
- Offline与network human approval已生成；ros-readonly PASS但尚未人工批准。下一步是physical现场approval，再批准ros-readonly。
- 2026-08-24 21:05 HKT操作员在无A2/PiPER command输出下逐轴人工移动，确认A2前12轴raw mapping与PiPER `arm_j1..arm_j6` state mapping全部PASS；完整direction/unit/zero/limits/stop表与`joint-validation` receipt仍待正式Gate 8。
- 已实际核对main A2 two-A源码：first A从当前q插值到default；second A进入history warmup，main约32帧/0.64秒，Stage2为30帧/0.60秒，源码`3000`只是日志throttle而非3秒delay。Stage2 direct已扩展为：first A锁存A2/PiPER实测position并以current hold等待操作员显式打开PiPER gate，fresh diagnostics确认open后两侧300 tick同步插值到manifest init，second A再warmup并于下一tickPolicyActive。`arm_j7/j8`不属于bridge v1控制维度。
- Candidate tag已在m45定向build PASS；真实state shadow ready、隔离A2/PiPER command均零消息。全局约1000 Hz bare-DDS `/lowcmd`已通过`CheckMode service='ai_sport'`和受保护ReleaseMode前后流量变化确认来自宇树官方控制链；release后mode为空且5秒计数为0。Release evidence：`.stage2_sessions/20260824_173406/evidence/a2-motion-release/20260824_213519`。
- 当前保持A2 official mode released供后续policy Gate；测试结束后的恢复入口固定为`stage2_gate.sh restore-a2`，目标`ai_sport`，恢复验收是`CheckMode service='ai_sport'`。Physical已批准，下一项是操作员批准ros-readonly，不是直接启动policy。
- Physical已由操作员批准；当前唯一receipt下一项是ros-readonly人工approval。
- Two-stage normal stop candidate已build和isolated no-output PASS：第一次L2+B同步回manifest reset hold，A重新warmup/resume，第二次L2+B停止PiPER并让A2插值到本机实测趴地目标。实测training-order目标为`[0.3602,-0.3789,0.3382,-0.3506,1.1862,1.1942,1.2177,1.1831,-2.7570,-2.7380,-2.7485,-2.7468] rad`；动作尚未hardware验证。Evidence：`.stage2_sessions/20260824_173406/evidence/pre-enable-stop-lifecycle/20260824_220228`。

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
- `docs/policy_host_m45.md`
