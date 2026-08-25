# A2 + PiPER Stage2 新手操作员唯一 Runbook

本文件是首次部署与日常 bring-up 的唯一执行入口。不要跳 Gate，不要把某一步的“预期 PASS”当成实际 PASS。每条 `stage2_gate.sh` 命令都会在 `.stage2_sessions/<session>/` 留下 receipt 和 stdout；命令失败后停在原 Gate，把 evidence 目录交给开发者。

Stage2 采用与本机 `main` locomotion 成功案例相同的 A2 direct 模式：C++ node 在同一进程内读取 `A2LowLevelInterface::latest_state()`，使用既有 mapping、PD 和 `publish_joint_commands()`，不需要新增 `/a2/joint_states` 或 `/a2/joint_command`。Python external-semantic transport 仍不可用，也不是本 Runbook 的 live 路径。

当前正式部署机是 `baoquanc@ai-precog-m45`，repo 位于 `/home/baoquanc/Workspace/GeneralSim2Real`。该机的真实 OS/GPU/NIC 与当前执行状态见 [policy_host_m45.md](policy_host_m45.md)；下文 m45 命令统一使用 `enp130s0`。

> m45 当前恢复点（2026-08-25 00:55 HKT）：最终dual-policy实机流程成功完成first-A同步init、second-A PolicyActive、两次显式14秒arm-goal轨迹与两段L2+B。成功live evidence为`evidence/live-both/20260825_004737_129916`，formal stop为`evidence/stop/20260825_005119_130865`。A2已恢复`ai_sport`，PC2 bridge已停止，m45无live container。下一版image为`a2-piper-stage2:manual-arm-rest-stop-20260825`：PolicyActive默认零arm task command并保持PiPER init，arm goal只接受第二终端显式命令；第二次L2+B会先让PiPER回first-A锁存的开机休息位，再quick stop。新语义已build PASS，尚待下一次实机验证。

### 2026-08-24 PiPER MIT/high-follow候选

Krushell fork的`piper_sdk/demo/V2/piper_set_mit.py`精确使用`MotionCtrl_2(1,1,0,0xAD)`。Stage2保持`JointCtrl`绝对关节位置接口，只把PC2 controller切到MOVE J MIT/high-follow；没有采用`JointMitCtrl`的逐电机`pos/vel/kp/kd/torque`接口。候选tag如下：

```text
a2-piper-stage2:mit-high-follow-20260824
a2-piper-pc2-bridge:mit-high-follow-20260824
```

PC2当前运行候选bridge；diagnostics实测`joint_control_mode=move_j_mit_high_follow`、`motion_ctrl_2=0x01,0x01,0,0xAD`且`command gate closed`。Stage2隔离namespace和全局topic观察均确认无LowCmd、无PiPER joint command，未调用enable/resume。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/mit-high-follow/20260824_221815
```

## 0. 先认清三台机器和三个终端

| 标记 | 机器/终端 | 用途 |
| --- | --- | --- |
| `P1` | `baoquanc@ai-precog-m45` 主终端 | 在 `/home/baoquanc/Workspace/GeneralSim2Real` 执行 Gate、前台运行 Stage2 |
| `P2` | 同一台 m45 的第二终端 | 看 status、调用明确获批的 PiPER service、执行 stop |
| `PC2` | `unitree@192.168.123.162`，hostname `unitree-a2-pc2` | USB【3】上的 USB-CAN owner；Docker/bridge image 已安装；本轮收尾后bridge停止 |

现场还必须有两个人：操作员拿 A2 remote；急停员只盯机械和 physical E-stop。任何人喊停都立即停止。

P1/P2 的所有命令固定从这里开始：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2
```

不要在另一个 clone 或本地 Mac 目录中执行现场 Gate。

### 0.1 固定SSH路径

本地Mac登录m45；若本机已有`Host m45`配置，优先用短名：

```bash
ssh m45
```

等价完整目标是：

```bash
ssh baoquanc@m45.precognition.team
```

进入m45后，再从m45登录A2 PC2。固定使用Stage2专用key和`.123.162`，不要使用PC2的`.124.162`：

```bash
ssh \
  -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes \
  unitree@192.168.123.162
```

PC2隔离项目根目录固定为：

```bash
cd /home/unitree/Workspace/baoquanc
```

密码不得写入命令、脚本、日志、文档或evidence。

### 0.2 下次实验的最短启动流程

先给A2与PiPER上电，清空工作区，准备遥控器、支撑和physical E-stop。登录m45后检查PC2 CAN；PC2重启后若`can_piper`不存在或不是`UP/ERROR-ACTIVE`，才重新执行既有activation：

```bash
ssh -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes unitree@192.168.123.162 \
  'PIPER_SDK_ROOT=/home/unitree/Workspace/baoquanc/src/piper_sdk \
   PIPER_CAN_NAME=can_piper PIPER_USB_ADDRESS=1-6:1.0 \
   /home/unitree/Workspace/baoquanc/runtime/activate_can.sh'
```

启动bridge并确认container为`Up`；启动本身保持command gate关闭，不调用resume/enable：

```bash
ssh -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes unitree@192.168.123.162 \
  /home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh start

ssh -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes unitree@192.168.123.162 \
  /home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh status
```

然后只需在m45主终端启动Stage2。`live`会先`motion-check → motion-release`，所以即使A2重启后`ai_sport`自动恢复，也会在创建LowCmd owner前按main经验正确交接：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2

STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component both \
  --live \
  --operator baoquanc
```

遥控器只做两次动作：第一次`A`触发一次`resume → enable`并直接开始A2/PiPER同步init插值；确认init完成、摇杆归中后，第二次`A`进入PolicyActive。此时base速度仍来自遥控器；PiPER保持init位，arm task command为`[0,0,0]`，不会自动运行position tracking或轨迹。

需要单点position tracking时，在m45第二终端输入球坐标目标`[radius_m,pitch_rad,yaw_rad]`：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2
./scripts/stage2_gate.sh arm-goal \
  --radius 0.4 --pitch 1.0472 --yaw 0.0 \
  --operator baoquanc
```

需要已固化的完整往返轨迹时输入：

```bash
./scripts/stage2_gate.sh trajectory --operator baoquanc
```

PC2 bridge v1只控制`arm_j1..arm_j6`。本文所说“保持gripper/末端起点”是保持PiPER六轴init位；`arm_j7/j8`夹爪仍没有实机command interface。

## 1. 已完成：m45 Ubuntu 与 Docker bootstrap（P1，无机器人输出）

当前已采集并验证：Ubuntu 24.04.4 LTS、`x86_64`、kernel `7.0.0-30-generic`、Docker Engine `29.7.2`、Compose `5.5.0`、NVIDIA RTX 5070 Ti。详情见 [policy_host_m45.md](policy_host_m45.md)。

日常恢复只做只读验证：

```bash
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only
```

精确 PASS：

```text
[PASS] Docker Engine, Compose v2, daemon access, and hello-world are ready.
```

失败才停止并交给开发者；不要重新安装 Docker、混装 `docker.io` 或改 daemon。

## 2. 已完成：offline 配置、image 和真实 bundle parity（P1，无机器人输出）

当前 `.env` 已固定为：

- `A2_NET_IFACE=enp130s0`
- `POLICY_HOST_IPV4=192.168.123.222/24`
- `ROS_DOMAIN_ID=0`
- `SITE_CONFIG_FILE=.../config/site.mock.yaml`
- `A2_PIPER_STAGE2_IMAGE=a2-piper-stage2:humble-torch2.7.0-cpu`

不要重复运行 `configure_policy_host.sh`。只读确认：

```bash
grep -E '^(A2_NET_IFACE|POLICY_HOST_IPV4|ROS_DOMAIN_ID|SITE_CONFIG_FILE|A2_PIPER_STAGE2_IMAGE)=' docker/.env
```

本次 session `20260824_173406` 的 offline 已 `PASS+APPROVED`。不要重跑 `init/offline`；只查看当前恢复点：

```bash
./scripts/stage2_gate.sh status
./scripts/stage2_gate.sh next
```

offline approval 已由操作员 `baoquanc` 记录；不要重复签署。

已保存的 offline evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/offline/20260824_192059_33754
```

已执行并保存的人工批准命令是：

```bash
./scripts/stage2_gate.sh approve --gate offline --operator baoquanc
```

这里确认的是模型/部署一致性。用户已确认 dual policy 完成 sim2sim，约 `0.039 m` 末态 arm position error 是 policy 效果 evidence，不是 deployment blocker；它也不能替代下面的硬件 mapping、watchdog 和逐 Gate 验收。

## 3. 暂停：不要创建或切换现场 direct site

当前没有 `config/site.yaml` 完成版；hardware limits、PiPER bridge watchdog、hold/stop/recovery 仍没有现场 evidence。继续使用 `config/site.mock.yaml`，不要复制 template、不要把 `.env` 指向 live site、不要设置 `output_enabled: true`。

m45 的 `enp130s0 / 192.168.123.222/24` 已存在且已连通 A2/PC2，不需要 `--apply-network`。只有网卡地址在重启后丢失，且现场网络负责人明确批准时，才回到 [policy_host_m45.md](policy_host_m45.md) 的恢复步骤。

### 2026-08-24 pre-enable candidate与A2官方控制权交接实测

已在m45以独立tag `a2-piper-stage2:preenable-init-20260824`定向重建新版direct runtime，没有覆盖原offline PASS image。三个ROS package均编译完成。随后强制`enable_motion=false`、`live_acknowledged=false`，读取真实A2/PiPER state并把A2/PiPER command与stop全部重映射到`/stage2_candidate/*`隔离namespace：node持续达到`mode=shadow;state=ready`，订阅新版`/piper/diagnostics`，两个隔离command observer完整超时且无消息。该结果是候选image no-output PASS，不是Stage2 Gate receipt，也没有验证first-A live phase。

同一次全局`/lowcmd` observer发现domain 0存在一个`_CREATED_BY_BARE_DDS_APP_` publisher，以约1000 Hz持续发布`unitree_hg/msg/LowCmd`。只读`MotionSwitcherClient::CheckMode()`实测`form='0', name='ai', service='ai_sport'`。在操作员明确授权后，沿用main A2 wrapper调用受保护`ReleaseMode()`：返回`ret=0`，随后两次`CheckMode()`均为mode空，5秒observer得到`lowcmd_count=0`。因此已确认此前bare-DDS流量来自宇树官方`ai_sport`控制链，正确交接方式是MotionSwitcher release，不是kill未知进程。

Release与停止流量evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/a2-motion-release/20260824_213519
```

当前保留released状态供后续自研policy Gate使用；不要启动第二个LowCmd publisher。测试完成后必须按第17节的`restore-a2`恢复`ai_sport`。

Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-init/20260824_212010
```

## 4. 已完成：Network Gate（P1，只读）

Network Gate 已于当前 session 实际执行并由 `baoquanc` 批准。m45 使用 `enp130s0 / 192.168.123.222/24`、domain 0；A2 PC1与PC2各3次ping全部成功。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/network/20260824_202236_44746
```

以下命令只保留为恢复记录，不要重复运行当前 receipt：

```bash
./scripts/check_policy_host.sh --connected
./scripts/stage2_gate.sh network \
  --iface enp130s0 \
  --domain-id 0 \
  --a2-ip 192.168.123.161 \
  --pc2-ip 192.168.123.162
```

精确 Gate PASS：

```text
PASS: network
```

该 PASS 表示 NIC 存在、IPv4/route 正确且 A2/PC2 都可 ping。任一 ping 失败就停止，不要换 Wi-Fi 绕过。

```bash
./scripts/stage2_gate.sh approve --gate network --operator baoquanc
```

## 5. 已完成：CAN feedback、command-gate-closed bridge 与 ROS read-only Gate（PC2 + P1）

PC2 bootstrap 已获批并完成：Ubuntu 22.04.4 host保留机器自带ROS/network/nginx配置；通过m45离线安装Docker 29.7.2、Compose 5.5.0和can-utils，导入amd64 bridge image，并把`krushell/piper_sdk 0.6.2` source、现场配置与运行入口放入：

```text
/home/unitree/Workspace/baoquanc/
├── src/piper_sdk/
├── bridge/piper_bridge.yaml
├── docker/compose.yaml
├── docker/piper_bridge.env
├── docker/image/
├── packages/
├── runtime/
└── evidence/
```

M45 offline staging evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pc2-bootstrap/20260824_210000
```

PC2 bootstrap PASS evidence：

```text
/home/unitree/Workspace/baoquanc/evidence/pc2_bootstrap_20260824_after_install.log
```

只读恢复检查：

```bash
/home/unitree/Workspace/baoquanc/runtime/verify_bootstrap.sh
```

精确 PASS：

```text
PASS: PC2 Docker/bridge bootstrap is installed; bridge stopped; CAN unchanged.
```

用户已明确授权并完成以下CAN activation；本机实测`can_piper UP/ERROR-ACTIVE`、1 Mbit/s、无bus error。3秒只读candump收到9,087帧，启动bridge前TX为0：

```bash
PIPER_SDK_ROOT=/home/unitree/Workspace/baoquanc/src/piper_sdk \
PIPER_CAN_NAME=can_piper \
PIPER_USB_ADDRESS=1-6:1.0 \
/home/unitree/Workspace/baoquanc/runtime/activate_can.sh

ip -details link show can_piper
timeout 10 candump can_piper
```

该路径固定本机实测USB interface `1-6:1.0`、bitrate `1000000`与目标名`can_piper`；没有改PC2 route、`eth0/net1`或机器自带服务。CAN evidence：

```text
/home/unitree/Workspace/baoquanc/evidence/can_feedback_before_bridge_20260824.log
```

Bridge已使用下列入口启动：

```bash
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh start
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh status
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh logs
```

启动、停止、日志与人工重启统一使用：

```bash
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh start
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh stop
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh logs
/home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh restart
```

Compose设置`restart: "no"`，PC2重启后不会自动重新占用CAN。恢复顺序固定为：只读核对USB path与`can0` → 重新执行获批的CAN activation → 确认feedback → `bridge_ctl.sh start` → diagnostics确认`command gate closed`。启动和重启都不得调用`enable/resume`。

首次启动暴露并修复两项image问题：ROS Humble setup不兼容entrypoint全程`set -u`；`python-can 4.6.1`需要包含`Self`的新版`typing-extensions`。最终image已在m45完成SDK import验证；最终PC2启动evidence：

```text
/home/unitree/Workspace/baoquanc/evidence/bridge_start_command_gate_closed_typing_fix_20260824.log
```

Diagnostics实测：`connected; command gate closed`、`command_gate_open=false`、`hardware_stop_required=false`、`arm_status=0`、joint/status feedback均200 Hz。SDK `ConnectPort()`的`PiperInit()`会发送13个limit/firmware查询帧；未调用enable/resume/stop或joint command。

M45 已使用专用key完成PC2非交互部署；密码未进入命令、repo或evidence。机器自带的focal nginx正在运行并监听80端口，APT存在其`libssl1.1`遗留依赖；本次安装未删除/替换nginx，也未运行`apt --fix-broken`，而是只用离线`dpkg`安装明确Docker/can-utils包。

bridge 安装完成并由 PC2 owner 确认只存在一个 CAN owner 后，才运行：

```bash
./scripts/probe_pc2_read_only.sh --ssh unitree@192.168.123.162 --session pc2_bridge_ready
./scripts/probe_ros_graph_read_only.sh --session piper_ros_graph_ready
./scripts/stage2_gate.sh ros-readonly --duration 10
```

精确 Gate PASS：

```text
PASS: ros-readonly
```

本次Gate已PASS，evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/ros-readonly/20260824_205245_59854
```

实时joint state包含`arm_j1..arm_j6`，position/velocity为6维rad/rad/s，实测50.000 Hz；同次A2 `/lowstate`约1052.7 Hz。Gate尚未人工批准，必须先完成下一节physical现场确认。

必须实际看到：

- `/lowstate` 类型 `unitree_hg/msg/LowState`；
- `/lowcmd` 类型 `unitree_hg/msg/LowCmd`；
- `/piper/joint_states` 类型 `sensor_msgs/msg/JointState`；
- PiPER names 同时包含 `arm_j1` 到 `arm_j6`；
- A2 和 PiPER state 都持续有频率输出。

不要用自动发现改topic名，也不要把A2已有的`/arm_Command`、`/arm_Feedback`当成PiPER bridge。

## 6. Gate 0 人工现场批准（P1 receipt，无软件动作）

逐项口头确认并现场观察：A2 已支撑、PiPER 工作区清空、physical E-stop 可达、急停员就位、A2 remote 的 `Select` 与 `L2+B` 已知、只有一个 LowCmd owner、只有一个 PiPER CAN owner。

此前约1000 Hz的bare-DDS owner已通过`CheckMode`与ReleaseMode前后流量变化确认是官方`ai_sport`；当前mode为空且5秒`/lowcmd`计数为0。LowCmd控制权交接前置已满足，但本节仍需操作员逐项完成全部physical checklist后才能签署。

确认后记录：

```bash
./scripts/stage2_gate.sh approve --gate physical --operator baoquanc
./scripts/stage2_gate.sh approve --gate ros-readonly --operator baoquanc
```

## 7. A2 既有 locomotion baseline（P1 + remote，真实 A2 动作）

这一 Gate 不运行 Stage2；它原样复用同一台A2已跑通的`a2_real_robot_test.sh`，不会重写A2 mapping/LowCmd逻辑。成功路径依次执行connected/LowState/joint、MotionSwitcher check/release、确认mode为空、`no-lowcmd`、既有policy、结束后的`no-lowcmd`和MotionSwitcher restore，全部输出进入同一evidence目录。必须先release再检查无流量，因为官方`ai_sport`运行时预期会持续发布LowCmd。

急停员 ready 后，在 P1 执行：

```bash
STAGE2_ALLOW_A2_BASELINE=1 \
A2_PIPER_STAGE2_IMAGE=a2-piper-stage2:mit-high-follow-20260824 \
./scripts/stage2_gate.sh a2-baseline \
  --iface enp130s0 \
  --duration 120 \
  --live \
  --operator baoquanc
```

操作顺序：

1. 第一按 `A`：stand-up interpolation；
2. 站稳并保持；
3. 摇杆居中，第二按 `A`：进入既有 locomotion policy；
4. 只做已批准的低幅命令；
5. `L2+B`：normal controlled-down；`Select`：immediate zero LowCmd software stop；
6. 必须在 120 秒结束前完成已批准的 stop。

精确脚本结果：

```text
PASS: a2-baseline
```

只有现场确认 stand/hold/low-amplitude/stop 都符合既有行为后才签字：

```bash
./scripts/stage2_gate.sh approve --gate a2-baseline --operator baoquanc
```

如果baseline中途FAIL，脚本会立即停止，可能尚未来得及restore MotionSwitcher。不要直接重跑；先按A2既有流程确认无LowCmd publisher、机器人已安全落地，再由现场负责人用`a2_real_robot_test.sh motion-check/motion-restore`恢复原mode。失败evidence会保留具体停在哪一步。

## 8. PiPER bridge baseline（P1，真实PiPER动作）

PC2 bridge read-only接口、physical、ros-readonly与A2 baseline receipt均已通过。本节是当前唯一下一项。清空PiPER前伸及回程工作区，并让急停员就位。

最终通过版本先锁存启动时实测关节位姿，显式resume后调用`/piper/enable`并等待MOVE J MIT/high-follow mode-ready：用10秒smoothstep前伸到`[0.00, 1.48, -0.63, -0.84, 0.00, 1.57] rad`，到位后保持5秒，再用10秒平滑回到刚才锁存的启动位姿，到位后调用`/piper/stop`。target验收为每轴最大误差`0.5 deg`，return单独使用`3.5 deg`，因为本轮实测J2/J3在休息姿态受重力、静摩擦和回差影响，不能用target的严格容差误判。必须显式使用最终定向image：

```bash
A2_PIPER_STAGE2_IMAGE=a2-piper-stage2:piper-baseline-return-tolerance-20260824 \
STAGE2_ALLOW_PIPER_BASELINE=1 \
./scripts/stage2_gate.sh piper-baseline \
  --live \
  --operator baoquanc
```

预期包含：

```text
round trip: transition=10.0s hold=5.0s
target max joint error=... deg
return max joint error=... deg
round-trip move smoke passed
PASS: piper-baseline
```

如果 bridge fault-latched且未获得resume授权，命令应失败。本轮resume已由操作员明确授权并记录；后续session不得继承该授权。

现场确认前伸、保持、返回启动位姿和stop均符合预期后签字：

```bash
./scripts/stage2_gate.sh approve --gate piper-baseline --operator baoquanc
```

本session已完成：target最大误差`0.478 deg`，return最大误差`2.743 deg`，动作效果由操作员确认，PiPER baseline PASS+APPROVED。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/piper-baseline/20260824_231903_98613
```

controller limit查询反馈六轴flash角度均为`[-180,180] deg`且最大速度均为`0.300 rad/s`。角度反馈比机械结构范围更宽，不能直接作为live hard limit；PiPER live采用官方SDK范围`J1 ±150°, J2 0..180°, J3 -170..0°, J4 ±100°, J5 ±70°, J6 ±120°`与controller/site的交集。50 Hz下`0.300 rad/s`对应每policy tick最多`0.006 rad`。

该定向image的无硬件输出预检已PASS：实时state/diagnostics可读、`command_gate_open=false`、未发送command。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/piper-roundtrip-preflight/20260824_224332
```

## 9. Gate 7 direct dry-run（P1，`enable_motion=false`）

当前 Gate 7 的真实能力是：加载真实 bundle，解析两路 state，完成 synchronized arm-first inference，并通过 status 报告 ready；A2 与 PiPER 都不发布命令。它不是“驱动已禁能的 command publish test”，不能据此声称 command path 已实机动作验证。

```bash
./scripts/stage2_gate.sh dry-run --duration 60
```

精确 PASS：

```text
PASS: dry-run
```

evidence 中必须有：

```text
contract=verified;mode=shadow;state=ready
```

同时Stage2 A2 publisher和observer共同使用隔离`/stage2_shadow/no_lowcmd`并必须零消息，PiPER command observer必须完整超时且没有收到`/piper/joint_command`。不能观察全局`/lowcmd`作为shadow归因，因为A2 baseline退出后官方`ai_sport`会恢复并正常发布该topic。

本session 60秒dry-run已经PASS，evidence为：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/dry-run/20260824_232605_100005
```

```bash
./scripts/stage2_gate.sh approve --gate dry-run --operator baoquanc
```

## 10. Gate 8 逐关节观察与人工验收（P1 observer + P2/现场负责人）

Gate 8不能由Stage2脚本自动移动关节：此时hardware-certified joint limits、方向和zero仍未知，自动构造target会反过来猜测本Gate要验证的事实。`joint-observe`自身只订阅状态，绝不发布A2 LowCmd或PiPER command。

### 2026-08-24 现场只读mapping预观察记录

在PC2 `can_piper`已UP、bridge保持`command gate closed`且没有调用`enable/resume`、没有发布`/piper/joint_command`或A2 `/lowcmd`的条件下，现场操作员逐一人工移动A2与PiPER关节并观察实时position。操作员确认：

- A2前12个`motor_state[index].q`按`0..11 = FR hip/body, FR thigh, FR calf, FL hip/body, FL thigh, FL calf, RR hip/body, RR thigh, RR calf, RL hip/body, RL thigh, RL calf`逐轴对应，全部PASS；对应Stage2 training→raw mapping仍为`[3,0,9,6,4,1,10,7,5,2,11,8]`。
- PiPER `/piper/joint_states`的`arm_j1..arm_j6`逐轴position响应与人工移动关节一致，全部PASS。
- 本次只确认关节身份、顺序和raw/bridge索引的实时响应；没有通过ROS/CAN下发动作target。

A2观察使用只订阅`/lowstate`的低刷新率终端表：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml \
  run --rm --no-deps policy-runtime \
  env A2_LIVE_PRINT_PERIOD=0.5 A2_JOINT_MIN_DELTA=0.01 \
  /opt/stage2/a2_scripts/a2_real_robot_test.sh joints-live
```

PiPER观察使用`/piper/joint_states`，名称顺序固定为`arm_j1..arm_j6`。这次操作员确认是正式Gate 8的mapping输入事实，但不能替代`joint_validation_table.tsv`中尚需审阅的positive direction、unit、zero、hardware-certified lower/upper limits和stop result，也没有生成`joint-validation` approval receipt。

开始前必须满足：A2稳定支撑、PiPER工作区隔离、physical E-stop operator就位。上述人工移动已确认read-only mapping，但没有批准或验证任何PiPER单关节命令程序，所以正式Gate 8仍停在动作与完整人工表验收之前。若仍没有经现场批准的PiPER单关节程序，不要临时编写target，也不要用dual-policy node代替。

P1在Terminal 1启动十分钟双路滚动observer：

```bash
./scripts/stage2_gate.sh joint-observe --duration 600
```

它同时显示并保存：

- A2 existing wrapper的`joints-live`，按raw motor index记录前12个关节的`q/dq`；
- `/piper/joint_states`的name、position、velocity；
- `joint_validation_table.tsv`人工表格模板。

observer运行期间，P2只用已经批准的既有单关节程序，一次只选择一个关节，并按该程序既有的最低验收幅度执行“移动—回到zero/hold—stop”。本Runbook不提供也不推导任何A2/PiPER target。limit值必须来自现场认可的硬件/bridge配置及其既有验证记录；不要为了填写上下限而撞机械限位。每个关节都必须由P1和现场负责人共同核对并填入表格：

```text
policy_joint / bridge_or_raw_index / observed_positive_direction / unit
zero_reference / observed_lower / observed_upper / stop_result / reviewer / notes
```

只有当前关节已经停止且其余关节没有非预期运动，才能开始下一关节。任一mapping、方向、单位、zero或stop结果不一致，立即使用现场批准的stop/E-stop，保留evidence并结束本Gate；不要换一个target继续试。

观察时间结束时精确PASS为：

```text
PASS: joint-observe
```

终端打印的evidence目录至少应包含：

```text
a2_joints_live.log
piper_joint_states.log
joint_validation_table.tsv
scope.receipt
```

现场负责人填完并逐行复核全部A2/PiPER关节后，才允许记录人工批准：

```bash
./scripts/stage2_gate.sh approve --gate joint-validation --operator baoquanc
```

该approval要求当前session已有`joint-observe` PASS，并在receipt中明确记录逐关节表的路径，以及mapping、direction、unit、zero、limits和stop result均已人工审阅。脚本会拒绝行数不是18或direction/unit/zero/lower/upper/stop/reviewer任一必填列为空的表；未完成全部行时不要执行approve。随后运行：

```bash
./scripts/stage2_gate.sh next
```

预期下一步才是`fault --scenario process-stop`。

本session现场Owner在已有逐轴mapping确认、A2 baseline和PiPER round-trip baseline均通过后，明确决定跳过该正式人工表，认为原安排过严。脚本已仅对本部署流程移除`fault`对`joint-validation` receipt的前置，不生成或伪造人工表/approval。只读observer PASS evidence仍保留：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/joint-observe/20260824_233131_101388
```

## 11. Fault Gate（P1，先自动化 no-output process-stop）

```bash
./scripts/stage2_gate.sh fault --scenario process-stop --duration 30
```

精确 PASS：

```text
PASS: fault
```

这个 receipt 只证明 shadow node 到达 ready 后能被停止；它明确不证明 live local watchdog。`approve fault` 前，现场还必须按 [safety_and_operations.md](safety_and_operations.md) 受控记录：policy host网络中断、A2/PiPER state stale、PC2 command timeout、feedback timeout、stop latch和人工 recovery。没有这些现场结果，不签字。

全部受控故障都符合现场 stop contract 后：

```bash
./scripts/stage2_gate.sh approve --gate fault --operator baoquanc
```

本session process-stop fault已PASS+APPROVED：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/fault/20260824_233346_101892
```

## 12. 十分钟 coupled shadow（P1，无命令输出）

```bash
./scripts/stage2_gate.sh shadow --duration 600
```

不要中途 `Ctrl+C`。精确 PASS：

```text
PASS: shadow
```

检查 status：持续为 `contract=verified;mode=shadow;state=ready`，没有 `state=blocked`，并且 A2/PiPER command observer 都没有收到消息。当前软件参数中的 age/skew 门限是 `200/200/50 ms`，但它们还不是现场批准的最终 watchdog contract；Gate 13 必须用实测 evidence 完成 site review 后才能用于 live。

```bash
./scripts/stage2_gate.sh approve --gate shadow --operator baoquanc
```

Owner授权跳过本session的中间shadow receipt；60秒coupled shadow已PASS且两路command observer零输出：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/shadow/20260824_233514_102361
```

## 13. Live 前才创建并完成 site.yaml

当前不要执行本节。只有 PiPER bridge、逐关节、fault/watchdog 和 hold/stop/recovery evidence 完整后，先创建现场文件：

```bash
cp config/site.template.yaml config/site.yaml
```

查看仍缺的现场字段：

```bash
grep -n 'TO_VERIFY\|output_enabled' config/site.yaml
```

从 Gate 8/9 的逐关节、limit、watchdog、hold/stop/recovery evidence 填写每一项。不要用 URDF limit冒充硬件 limit。完成 review 后，operator本人用编辑器把：

`site_limits.*_joint_position_rad`中的每个joint都必须是`[lower, upper]`两项rad；`*_target_rate_rad_per_policy_tick`中的每个joint都必须是正数rad/20 ms；`consecutive_deadline_miss_limit`必须是正整数。不能删除joint、改名或增加joint。Live preflight会用canonical C++ parser核对exact key set、finite值、default position、manifest/site limit交集和rate交集。

```yaml
safety:
  output_enabled: false
```

手动改为：

```yaml
safety:
  output_enabled: true
```

`stage2_gate.sh` 不会替你修改它，也不会调用 `/piper/resume`。

本session Owner已授权创建完成版`config/site.yaml`用于隔离preflight：A2采用bundle/main baseline边界；PiPER采用官方机械范围与controller反馈交集，rate为`0.006 rad/20 ms`；deadline为20 ms、连续5次miss停止。`docker/.env`已切换至该文件。canonical `live-both` preflight已PASS，未调用enable/resume且未发布hardware command：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/live-preflight-both/20260824_234059_105233
```

## 14. Gate 11A dog-only live（P1/P2，真实 A2 动作）

先记录 component-specific approval：

```bash
./scripts/stage2_gate.sh approve --gate live-dog_only --operator baoquanc
./scripts/stage2_gate.sh live-preflight --component dog_only --operator baoquanc
```

精确 PASS：

```text
PASS: canonical C++ live site loaded on isolated dummy topics
PASS: live preflight sent no enable/resume/hardware output command
PASS: live-preflight-dog_only
```

Preflight会让canonical C++ parser加载完成版site并应用limit/rate/deadline，但把A2/PiPER state、command、stop和status全部改到isolated dummy topics；因此它能在MotionSwitcher release之前暴露site结构/limit交集错误，不会触碰硬件输出。

P2 准备好 stop 命令但先不要执行：

```bash
./scripts/stage2_gate.sh stop --operator baoquanc --reason "dog-only operator stop"
```

P1 启动真实路径：

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component dog_only \
  --live \
  --operator baoquanc
```

在启动direct node前，`live`会原样调用A2 existing wrapper执行MotionSwitcher check/release；release失败时node不会启动。它不会自动restore，因为restore必须在确认A2已安全落地、没有LowCmd publisher后由operator决定。

操作：第一 `A`从当前躺地姿态开始A2 init interpolation；默认`150+150=300`个50 Hz tick，共约6秒。完成并确认`phase=StandHoldWaitingForA`、`a2_output=init_position_hold`，站稳且摇杆归中后第二 `A`。第二 `A`不是固定等待3秒，而是保持init pose并填满30-frame Stage2 history，约0.60秒；下一tick才进入PolicyActive。只做低幅动作。`dog_only`仍运行arm actor生成body plan，但不发布PiPER command。status 的 PolicyActive 必须显示：

```text
component=dog_only
a2_output=dog_actor_target
piper_output=not_published
```

正常停止按第17节使用两段`L2+B`；第一次回reset hold，第二次才结束测试并趴地。异常立即用`Select`；若node/remote失效，P2执行准备好的`stage2_gate.sh stop`。physical E-stop始终优先于软件。

## 15. Gate 11B arm-only live（P1/P2，真实 A2 hold + PiPER 动作）

每个 component 都要独立 approval/preflight：

```bash
./scripts/stage2_gate.sh approve --gate live-arm_only --operator baoquanc
./scripts/stage2_gate.sh live-preflight --component arm_only --operator baoquanc
```

先在P1启动node：

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component arm_only \
  --live \
  --operator baoquanc
```

此时不要按第二次`A`，也不要在第一次`A`之前提前enable PiPER；bridge在enable后要求0.20秒内收到fresh command。第一次按`A`后，direct node锁存A2/PiPER当前position，A2以低起始gain保持当前姿态，并持续发布PiPER measured-position hold。gate关闭时这些PiPER消息会被bridge丢弃；enable完成后的下一条fresh hold会被接收，从而避免打开gate时跳到插值中途target。P2先确认Stage2 status出现：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 topic echo --once /a2_piper_stage2/status --field data
```

必须包含：

```text
mode=live
phase=InitHoldWaitingForPiperGate
piper_output=measured_position_hold
```

然后P2读取PC2状态：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 topic echo --once /piper/diagnostics
```

如果diagnostics显示`manual_stop`或`quick_stop_latched`，只有PC2 owner确认可恢复后，P2才显式调用：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 service call /piper/resume std_srvs/srv/Trigger '{}'
```

如果没有latch，不调用resume。接着P2显式enable：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 service call /piper/enable std_srvs/srv/Trigger '{}'
```

立即再次读取diagnostics：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 topic echo --once /piper/diagnostics
```

必须看到`message: command gate open`，并在values里看到`key: command_gate_open`对应`value: 'true'`。direct node从`/piper/diagnostics`收到fresh open状态后，才自动启动A2与PiPER同步init interpolation；默认`150+150=300`个50 Hz tick，共约6秒。目标完全来自bundle manifest：

```text
A2 training order:
hip   = [0.0, 0.0, 0.0, 0.0]
thigh = [0.5, 0.5, 0.5, 0.5]
calf  = [-1.0, -1.0, -1.0, -1.0]

PiPER:
[arm_j1, arm_j2, arm_j3, arm_j4, arm_j5, arm_j6]
= [0.00, 1.48, -0.63, -0.84, 0.00, 1.57] rad
```

`arm_j7=0`、`arm_j8=0`是训练时固定gripper target；当前PC2 bridge v1没有这两个command interface，不能发布，也不能把arm actor的plan两维解释成它们。

插值期间status必须显示`phase=StandUpInterpolating`以及两侧`*_output=init_position_interpolation`。完成后必须看到：

```text
phase=StandHoldWaitingForA
a2_output=init_position_hold
piper_output=init_position_hold
```

否则不要按第二次`A`，执行stop。只有A2站稳、PiPER init hold稳定、摇杆归中，才按第二次`A`。第二次`A`进入30-frame warmup hold，约0.60秒，并在下一tick进入PolicyActive；没有固定3秒延时。`arm_only`为保持base稳定仍向A2发布init-position PD hold，不是“A2完全无输出”。PolicyActive status必须显示：

```text
component=arm_only
a2_output=default_position_hold
piper_output=arm_actor_target
```

当前live配置使用球坐标task-space goal轨迹`[radius_m, elevation_pitch_rad, yaw_rad]`：进入`PolicyActive`时从`[0.4, 1.0472, 0.0]`开始，以smoothstep在6秒内到`[0.4, -1.2566, 0.0]`并保持。manifest记录的训练采样pitch范围是`[-1.0, 1.0] rad`，所以终点比采样下界多`0.2566 rad`；它不是PiPER joint angle，不能与J1–J6机械限位直接比较。本轨迹按训练端给出的任务目标保留，实际PiPER输出仍由完成版site按官方机械范围及`0.006 rad/20 ms`限幅。正常停止使用第17节的两段`L2+B`：第一次让A2/PiPER回reset hold，不调用`/piper/stop`；第二次才停止PiPER并让A2趴地。

## 16. Gate 12 both live（支撑/夹具、无接触）

本session由Owner明确跳过dog-only/arm-only中间receipt并直接推进both；这不替代真正live-both的最后一次人工approval。当前candidate preflight已完成，只需先签署live-both：

```bash
./scripts/stage2_gate.sh approve --gate live-both --operator baoquanc
```

最终无输出preflight使用image `a2-piper-stage2:dual-goal-pitch-trajectory-20260824`，evidence为：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/live-preflight-both/20260824_234854_107096
```

其status已核对`goal_trajectory=enabled`、起点/终点/6秒时长，且`a2_output=not_published`、`piper_output=not_published`。

最终live-both操作为：启动node → 第一次`A`后节点只调用一次PiPER resume/enable并执行约6秒A2/PiPER init interpolation → 确认init完成、摇杆归中 → 第二次`A` → 约0.60秒history warmup → 1秒smooth dog-output handover。进入PolicyActive后base移动仍由遥控器给速度；PiPER保持init位，arm task command默认为`[0,0,0]`，不会自动启动position tracking或轨迹。只有m45第二终端的`arm-goal`或`trajectory`命令才启用arm actor target输出。不再要求单独的diagnostics/resume/enable/UI终端。

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component both \
  --live \
  --operator baoquanc
```

第一次`A`之前PiPER command gate保持关闭；第一次`A`锁存两侧实测位置后，direct node只发起一次`resume → enable`，fresh diagnostics确认gate open后才开始init interpolation。若任一service拒绝，不会自动反复调用，按`B`取消本轮handover并查看前台错误。Dog-only永远不resume、不enable PiPER。

PolicyActive且尚未收到arm命令时status必须显示：

```text
component=both
a2_output=dog_actor_target
piper_output=init_position_hold
arm_tracking=idle_zero_hold
goal_trajectory=enabled
goal_trajectory_state=armed
goal_r=0.000
goal_pitch=0.000
goal_yaw=0.000
goal_duration_s=14.000
```

第二次`A`后确认前台日志已经进入PolicyActive。单点position tracking命令为：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2
./scripts/stage2_gate.sh arm-goal \
  --radius 0.4 --pitch 1.0472 --yaw 0.0 \
  --operator baoquanc
```

已固化往返轨迹命令为：

```bash
cd /home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2
./scripts/stage2_gate.sh trajectory --operator baoquanc
```

成功输出为`PASS: round-trip arm-goal trajectory started`。一次trigger依次执行：4秒从init goal到`[0.4,1.0472,0]`，6秒到`[0.4,-1.2566,0]`，4秒回init goal；重复执行会重新开始完整14秒轨迹。

历史候选image为`a2-piper-stage2:dual-triggered-trajectory-20260824`。隔离dummy topics/services验证确认`goal_trajectory_state=armed`、A2/PiPER output均not published，并确认PolicyActive前的trajectory请求被拒绝：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/triggered-trajectory-preflight/20260825_000056_112019
```

2026-08-25首次first-A实机尝试中，resume/enable均成功，但旧direct node继续等待下一帧gate-open diagnostics，没有进入init interpolation；操作员在第二次A前退出。`Ctrl+C`同时暴露旧runner未把signal传给后台compose进程，残留container随后由正式`stage2_gate.sh stop`清理，双路径stop evidence为：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/stop/20260825_000456_113403
```

最终修复image为`a2-piper-stage2:dual-triggered-handoverfix-20260825`。它使用成功的`/piper/enable`响应作为本轮fresh gate-open handover确认，下一20 ms tick立即发送measured hold并切入init；diagnostics仍继续更新真实gate状态。command topic保持与PC2 bridge完全一致的BEST_EFFORT QoS。`Ctrl+C/SIGTERM`现在自动调用正式dual-path stop，不再遗留live container。无输出graph/site验证evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/handover-fix-preflight/20260825_000936_116144
```

第二次first-A实机尝试验证了handover seed，但A2仍未开始init：enable成功后约263 ms，PiPER diagnostics再次报告gate closed。对应live evidence为`evidence/live-both/20260825_001224_116559`；操作员`Ctrl+C`后新版trap完成PiPER stop和A2 zero-LowCmd并清理container，stop evidence为`evidence/stop/20260825_001300_117022`。根因是旧direct executable使用single-threaded executor，约1 kHz `/lowstate`与PiPER callbacks使50 Hz control timer超过200 ms没有执行，触发bridge command watchdog；不是init姿态、PiPER command QoS或A2官方mode。

最终现场image更新为`a2-piper-stage2:dual-triggered-executorfix-20260825`：control timer和trajectory service位于独立mutually-exclusive callback group，进程使用2-thread `MultiThreadedExecutor`，保证state callbacks与50 Hz control tick并行且policy tick自身不重入。真实A2/PiPER state负载下8秒command-remapped shadow收到35条status，隔离A2/PiPER output均为零：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/executor-shadow-preflight/20260825_001628_118549
```

每次上述`stage2_gate.sh live`在创建direct node前都会执行`motion-check → motion-release`；若`ai_sport`在两次试验间自动恢复，它会在本次live中重新release，release失败则node不会启动。第二次失败退出后于00:19 HKT再次只读实测`CheckMode ret=0 form='0' name='' service=''`、5秒`lowcmd_count=0`且无live container，明确排除官方mode仍在运行。`Ctrl+C`触发的formal stop只停止Stage2输出和PiPER，不代表恢复`ai_sport`；结束全部测试后才按第17节显式`restore-a2`。

重新测试first A后必须依次看到：

```text
PiPER automatic resume accepted
PiPER automatic enable accepted and handover gate seeded open
PiPER command gate open: starting synchronized A2/PiPER init interpolation
A2/PiPER init interpolation complete
```

缺少第三行或机器人未开始平滑运动时，不按第二次A，直接`Ctrl+C`；新runner会自动stop并清理container。

第三次live evidence `evidence/live-both/20260825_002043_119337`确认executor修复后A2 first-A init成功。second A后A2出现一次剧烈接管并保持明显正pitch，PiPER进入quick-stop；操作员随后完成两段L2+B并退出，stop evidence为`evidence/stop/20260825_002234_120016`。旧trajectory在warmup期间已经使用高仰角起点goal，且A2 site每tick上限大于actor约0.25 rad输出幅度，所以第一帧几乎没有handover限幅。新image `a2-piper-stage2:dual-roundtrip-handover-20260825`改为PolicyActive先保持init goal，并用50 tick/1秒smoothstep接管actor target；真实state、command-remapped验证evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/roundtrip-handover-preflight/20260825_002957_122308
```

PC2时钟实测比m45慢约36.5秒；对齐bridge log后，PiPER fault是enable后约0.15秒的`arm_status=5`（joint communication abnormal），不是command timeout。当前只读CAN status/err_code已恢复0。bridge image `a2-piper-pc2-bridge:stable-enable-20260825`要求连续0.5秒`arm_status=0 && ctrl_mode=1`才返回enable成功；运行期故障仍立即quick-stop。重启验收为command gate closed、hardware_stop_required=false、arm_status/ctrl_mode=`0/0`、joint/status 200 Hz。

第四次first-A中，stable-enable成功但A2未进入init，PC2随后记录`command_timeout`。Live/stop evidence为`evidence/live-both/20260825_003656_125155`和`evidence/stop/20260825_003710_125606`。这是因为PC2单线程enable callback连续检查0.5秒稳定状态时暂停了joint-state publisher，Stage2在等待service期间看到PiPER state超过200 ms并重置first-A lifecycle。新image `a2-piper-stage2:handover-stale-fix-20260825`仅在`InitHoldWaitingForPiperGate`且enable request仍in-flight时容许该预期空窗，持续发送锁存hold；其余phase仍严格执行200 ms stale stop。隔离fixture保持A2 state 50 Hz、故意让dummy PiPER owner阻塞0.70秒，最终enable返回后约18 ms进入同步init：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/handover-stale-preflight/20260825_004327_128268
```

最终实机run使用上述修复成功完成完整流程：first-A同步init、second-A进入PolicyActive、两次显式14秒轨迹、第一次L2+B回reset、第二次L2+B让A2趴地并结束。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/live-both/20260825_004737_129916
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/stop/20260825_005119_130865
```

该run同时确认旧实现的一个缺口：第二次L2+B立即quick-stop PiPER，所以只有A2回到趴地位。新image `a2-piper-stage2:manual-arm-rest-stop-20260825`改为第二次L2+B同步将A2送到实测趴地位、PiPER送回本轮first-A锁存的开机休息位，完成后才请求`/piper/stop`。该改动已在m45完成target build，尚待下次实机验证，不能写成hardware PASS。

重点观察`goal_pitch`从`1.0472`平滑降到`-1.2566`、`goal_progress`从`0`到`1`，以及`plan_body_pitch`和实测`base_pitch`是否随之出现对应响应；`plan_body_roll`与`base_roll`用于确认是否产生伴随roll。这里plan字段顺序来自arm actor的`[body_pitch, body_roll]`，base字段是实测`[roll, pitch]`，已拆成独立标量避免混淆。

Gate 12 只允许支撑/夹具、无任务接触。自由站立和任务接触分别属于 Gate 13/14，需要新的现场计划和批准，不由本脚本自动扩大动作范围。

## 17. Stop、状态与证据

随时查看当前 Gate：

```bash
./scripts/stage2_gate.sh status
./scripts/stage2_gate.sh next
```

### 正常停止、恢复policy与结束测试

`L2+B`是normal stop组合键，按`B`的rising edge触发；按住不放不会连续跨越两个阶段。`Select`仍是immediate zero LowCmd software stop，不承担平滑流程。

第一次`L2+B`：立即reset policy/history，锁存A2与PiPER当前position，以250个50 Hz tick、约5秒smoothstep同步回manifest reset/init pose。此阶段不调用`/piper/stop`。预期status依次为：

```text
phase=ResetInterpolating
a2_output=reset_position_interpolation
piper_output=reset_position_interpolation

phase=ResetHoldWaitingForAOrStop
a2_output=reset_position_hold
piper_output=reset_position_hold
```

到达reset hold后有两个选择：

- 摇杆归中后按`A`：进入30-frame、约0.60秒`PolicyWarmupHold`，下一tick重新进入`PolicyActive`；不重复起身插值，因为两侧已经处于reset pose。
- 松开停止组合键后再次按`L2+B`：选择结束测试。A2以250个50 Hz tick、约5秒smoothstep插值到本机实测趴地目标；PiPER同时从reset位回到本轮first-A时锁存的开机休息位。两侧回程完成后才调用`/piper/stop`，A2最后持续`HoldProne`。不要在PiPER回程中提前`Ctrl+C`，除非出现异常需要immediate stop。

实测趴地目标来自2026-08-24 21:47 HKT在m45只读采集的5263个有效LowState样本；5秒内每轴range均不超过`0.0001 rad`。Training order目标为：

```text
[FL_hip, FR_hip, RL_hip, RR_hip] = [ 0.3602, -0.3789,  0.3382, -0.3506]
[FL_thigh, FR_thigh, RL_thigh, RR_thigh] = [1.1862, 1.1942, 1.2177, 1.1831]
[FL_calf, FR_calf, RL_calf, RR_calf] = [-2.7570, -2.7380, -2.7485, -2.7468]
```

采集与最终candidate无输出evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-stop-lifecycle/20260824_214754
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-stop-lifecycle/20260824_220228
```

旧候选tag是`a2-piper-stage2:preenable-stop-lifecycle-20260824`。最终实机已验证第一次L2+B reset和A2第二段趴地，但旧代码没有PiPER第二段回程。补齐PiPER回first-A开机休息位的新tag是`a2-piper-stage2:manual-arm-rest-stop-20260825`，已build PASS，第二段PiPER回程仍待下一次hardware验证。

### Immediate software stop

软件 stop（P2）会中断当前 live container、调用 `/piper/stop`，再原样复用 A2 verified wrapper发布 zero LowCmd。A2 可能失去支撑力，因此只在immediate stop场景使用：

```bash
./scripts/stage2_gate.sh stop \
  --operator baoquanc \
  --reason "写明原因"
```

精确 PASS：

```text
PASS: direct process interrupted, PiPER stop called, verified A2 zero-LowCmd path completed
```

如果这个 PASS 没出现，立即用 physical E-stop，不重复重试命令。

A2已经安全支撑/落地、Stage2 live container已退出并确认`no-lowcmd PASS`后，优先使用Stage2恢复入口。它固定恢复目标为本次release前实测的`ai_sport`，内部顺序是`no-lowcmd 5` → guarded `motion-restore` → `motion-check`：

```bash
STAGE2_ALLOW_A2_RESTORE=1 \
./scripts/stage2_gate.sh restore-a2 \
  --iface enp130s0 \
  --operator baoquanc
```

精确成功结果：

```text
CheckMode ret=0 form='0' name='ai' service='ai_sport'
PASS: A2 official motion mode restored to ai_sport
```

恢复成功后停止PC2 bridge。这个动作只停止container；不要在关机收尾时再次调用PiPER resume/enable：

```bash
ssh -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes unitree@192.168.123.162 \
  /home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh stop

ssh -i /home/baoquanc/.ssh/id_ed25519_pc2_stage2 \
  -o IdentitiesOnly=yes unitree@192.168.123.162 \
  /home/unitree/Workspace/baoquanc/runtime/bridge_ctl.sh status
```

最终`status`应为空表，m45上的`docker ps --filter name=stage2-live-<session>`也不应显示live container。到这里软件安全收尾完成，A2/PiPER的系统关机或断电由现场操作员执行。

2026-08-25本轮已实际执行：`no-lowcmd`计数0、`SelectMode('ai_sport') ret=0`、最终`motion-check service='ai_sport'`、PC2 bridge stopped、m45 live container为空。A2恢复evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/a2-restore/20260825_005440_131866
```

如果Stage2 Gate wrapper本身不可用，现场负责人可使用同一底层main A2 wrapper恢复：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  -e A2_ALLOW_SELECT_MODE=1 -e A2_MOTION_RESTORE_MODE=ai_sport \
  policy-runtime /opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh \
  motion-restore enp130s0

docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime /opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh \
  motion-check enp130s0
```

不要在A2仍站立、Stage2 container仍运行或`no-lowcmd`未通过时执行restore。恢复后`ai_sport`会重新成为官方LowCmd owner，因此此时`no-lowcmd`再次出现官方流量是预期行为；验收依据是`motion-check`回到`service='ai_sport'`，而不是restore后再次要求no-lowcmd。

所有 session evidence 位于：

```text
deploy/a2_piper_stage2/.stage2_sessions/<id>/
├── session.receipt
├── approvals/
├── results/
└── evidence/<step>/<timestamp>/
```

每个receipt记录实际命令、时间、operator、退出码和evidence路径。不要删除失败evidence，也不要把新session的approval复制到旧session。
