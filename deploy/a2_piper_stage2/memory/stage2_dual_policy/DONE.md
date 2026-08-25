# DONE

## 2026-08-24 HKT

- [x] 在 PC2 bridge 基线分支建立独立 `deploy/a2_piper_stage2/`。
- [x] 导入真实 Stage2 dog/arm TorchScript、manifest、metadata 和 parity references。
- [x] 实现 manifest-driven arm-first runtime、30-frame history、dog same-tick preview、raw last-action cache、named action processing、CPU parity/benchmark/mock CLI。
- [x] 分离 nominal policy target 与 LMP-URDF/rate-limited target语义。
- [x] 确认当前分支复用 main 已成功 A2 locomotion 路径，新增 C++ `a2_piper_stage2_direct` 而不新增 A2 semantic bridge。
- [x] 实现 live site limit/rate contract、deadline miss、two-A handover、warmup limiter seed、PiPER semantic output 和分组件 mode。
- [x] 实现 Ubuntu/Docker bootstrap、CPU Torch/LibTorch 2.7 image、host/PC2/ROS probes、session Gate runner 和中文操作员 Runbook。
- [x] Gate runner加入只读`joint-observe`双路采集、逐关节人工表格与`joint-validation` approval前置；不在未知site limits时构造动作target。
- [x] 保留尚未现场验证的 limits/watchdog/stop/recovery 为 `TO_VERIFY`，不把代码存在写成实机 Gate 已通过。
- [x] 自主采集正式 policy host `ai-precog-m45` 的真实 OS/GPU/RAM/disk/NIC/repo 信息，确认 robot NIC 为 `enp130s0` / `192.168.123.222/24`，并建立 `docs/policy_host_m45.md`。
- [x] 将 Docker bootstrap 扩展到 Docker 官方支持的 Ubuntu 24.04 Noble，同时保留 22.04 Jammy；Runbook 与 template 改用 m45 的真实 NIC。
- [x] 在 m45 写入真实 bundle + mock site 的 `docker/.env`，并初始化全 PENDING 的 Stage2 session `20260824_173406`；未预先批准任何 Gate。
- [x] 在m45安装并验证Docker Engine 29.7.2、Compose v5.5.0、overlayfs与非sudo daemon access。
- [x] 在目标Ubuntu 24.04 host构建Ubuntu 22.04/ROS2 Humble CPU image，编译A2 low-level、Stage2 direct与PiPER bridge packages。
- [x] 修复目标构建暴露的A2 export-set、Humble timestamp API、Jammy pip PEP 621 metadata与C++/Python LibTorch library-shadow问题。
- [x] Session `20260824_173406` offline Gate PASS：真实bundle parity全部`<=1e-6`，2000 pair benchmark max `0.366795 ms`，500 tick mock shadow PASS且无hardware output。
- [x] 接线后m45 `enp130s0 / 192.168.123.222/24`恢复，PC1`.161`与PC2`.162`可达；A2-only ROS read-only probe与约1052.7 Hz LowState观测PASS。
- [x] 完成PC2/USB-CAN只读盘点，确认USB识别但CAN/SDK/bridge尚未配置；严格停在任何写操作之前。
- [x] Session `20260824_173406` 的offline与network均由操作员记录`PASS+APPROVED`；network evidence确认m45经`enp130s0`可达PC1/PC2。
- [x] 在m45构建amd64 PiPER bridge image并准备86个Ubuntu22.04离线包，经`.123.162`部署到PC2隔离目录。
- [x] PC2离线安装Docker29.7.2、Compose5.5.0、can-utils与bridge image，提取实际SDK source；bootstrap验证PASS且bridge/CAN保持停止。
- [x] 获批将PC2 USB-CAN配置为1 Mbit/s`can_piper`，candump feedback PASS；command-gate-closed bridge与实时6关节state 50 Hz PASS。
- [x] 修复PC2 image的ROS setup nounset与`typing_extensions.Self`依赖，最终SDK import、bridge startup和ros-readonly Gate PASS。
- [x] 2026-08-24 21:05 HKT，操作员在只读、无A2/PiPER command输出下逐一人工移动关节，确认A2前12轴raw index/label和PiPER `arm_j1..arm_j6` state mapping全部PASS；未把该预观察冒充完整`joint-validation` Gate。
- [x] 实际核对main A2 two-A状态机，确认second A后是history warmup而非固定3秒；Stage2 direct加入PiPER diagnostics gate确认、current-position hold、A2/PiPER同步300-tick manifest-init interpolation，以及second A后的30-frame warmup/next-tick handover。未执行硬件enable或command。
- [x] 在m45以独立candidate tag定向build新版Stage2 image；新版binary读取真实A2/PiPER state达到shadow ready，订阅`/piper/diagnostics`，隔离A2/PiPER command observer均完整超时零消息。保留全局no-lowcmd因既有bare-DDS约1000 Hz publisher而FAIL的evidence，未升级任何Gate。
- [x] 只读`CheckMode`确认A2官方mode为`ai_sport`；经操作员授权执行guarded `ReleaseMode ret=0`，两次后验mode均为空且5秒`/lowcmd`计数为0，严格确认此前bare-DDS流量的官方owner与正确交接方式。
- [x] 新增`stage2_gate.sh restore-a2`恢复入口：live container必须停止，依次要求`no-lowcmd`、guarded SelectMode恢复`ai_sport`、`motion-check service='ai_sport'`；本轮保持released而未恢复。
- [x] 操作员为session `20260824_173406`记录physical approval；AI未代签。
- [x] 只读采集当前A2静止趴地姿态5秒/5263个有效样本并写入12轴training-order目标；未发布LowCmd。
- [x] 实现two-stage L2+B normal stop candidate并在m45定向build；最终image真实state shadow ready，全局/隔离A2 LowCmd与隔离PiPER command observer均零消息。动作hardware验证仍待后续Gate。

## 2026-08-25 HKT

- [x] Final dual live完成first-A自动PiPER resume/enable与A2/PiPER同步init、second-A PolicyActive、两次显式14秒arm-goal轨迹、reset/A2 prone stop及formal dual-path stop。
- [x] 测试结束后`no-lowcmd`、guarded `SelectMode('ai_sport')`与最终`CheckMode service='ai_sport'`通过；PC2 bridge停止。
- [x] 新build将PolicyActive默认行为改为零arm task command + PiPER init hold，并加入显式`arm-goal`入口；第二次L2+B改为PiPER回first-A启动休息位后再stop。Build PASS，后一项hardware验证保留为TODO。
