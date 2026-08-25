# A2 PC2 + PiPER 只读实机盘点（2026-08-24）

本页首先记录从正式 policy host `ai-precog-m45` 经 A2 switch-1 对 `unitree-a2-pc2` 完成的只读采集；后文另记随后获批完成的Docker、CAN与command-gate-closed bridge。没有调用PiPER service或发布joint command；登录凭据不进入本页、repo或evidence。

## 网络与主机

| 项目 | 实测值 |
| --- | --- |
| hostname / user | `unitree-a2-pc2` / `unitree` |
| OS | Ubuntu 22.04.4 LTS |
| kernel | `6.5.2-rt8`，`PREEMPT_RT`，x86_64 |
| CPU | Intel Core i7-1355U，10 cores / 12 logical CPUs |
| memory / swap | 30 GiB / 15 GiB |
| root disk | 468 GiB，总可用约382 GiB（采集时） |
| time | Asia/Shanghai，NTP synchronized |
| `eth0` | `192.168.123.162/24`，A2 switch/DDS运行路径；m45可ping与SSH |
| `net1` | `192.168.124.162/24`，Realtek RTL8153 USB Ethernet |
| m45直达结论 | `.123.162` PASS；`.124.162`不可由m45当前`.123`直连访问 |
| default route | 无；PC2当前不能按普通联网主机直接下载apt/git/container内容 |

M45同一根A2网络同时可达PC1 `192.168.123.161`与PC2 `192.168.123.162`。因此runtime、probe和后续bridge统一使用PC2 `.123.162`；`.124.162`只记录为PC2第二网口地址，不给m45增加猜测路由。

## USB-CAN 与 SocketCAN

PiPER通信线连接到用户标记的PC2 USB【3】后，kernel在`2026-08-24 19:39:04 +08:00`枚举：

```text
USB bus/device: 001/004
physical kernel path: pci-0000:00:14.0-usb-0:6
USB ID: 1d50:606f
model: candleLight USB to CAN adapter
vendor: bytewerk / OpenMoko database entry
driver: gs_usb
serial: 003100365343570F20363330
SocketCAN interface: can0
```

只读盘点当时状态：

```text
can0: DOWN
CAN state: STOPPED
RX packets: 0
TX packets: 0
can_piper: does not exist
candump/cansend: unavailable
can-utils: not installed
```

这证明USB模块、线缆到PC2和kernel driver已经识别；它不证明CAN bitrate、PiPER feedback、firmware/API或motor状态。只读阶段没有执行`ip link set`、rename、bitrate配置或SDK activation script。

## PiPER software/bridge

采集时：

- PC2 host未安装Docker/Compose。
- login shell中没有`piper_sdk`、`piper_sdk_interface`或`piper` Python module。
- `/opt`、`/usr/local`与`/home/unitree`的有限深度搜索未找到PiPER目录。
- 没有PiPER/CAN/bridge process或相关system service。
- 没有`/piper/joint_states`、`/piper/diagnostics`或任何repository contract中的`/piper/*` interface。

只读盘点当时不存在CAN command owner；也没有read-only PiPER feedback owner。

PC2当前没有default route。后续安装应优先在m45准备并经`.123.162`传入经过review的SDK、container image与所需离线package；不得为了方便由AI临时修改PC2 route或系统网络。

## 随后获批完成的 Docker/bridge bootstrap

操作员批准PC2 Docker/bridge建设后，m45构建并经`.123.162`传入amd64 image与Ubuntu22.04离线包。PC2当前实测：

- Docker Engine `29.7.2`、Compose `5.5.0`、overlayfs与非sudo daemon access可用；
- `/usr/bin/candump`可用；
- image `a2-piper-pc2-bridge:humble-20260824`已导入；image内`piper-sdk 0.6.2`与`piper_bridge`已构建；
- `/home/unitree/Workspace/baoquanc/src/piper_sdk`是从实际image提取的SDK source tree，包含`GetArmHighSpdInfoAverage`；
- Compose/config/runtime/package/image/evidence全部位于`/home/unitree/Workspace/baoquanc/`；
- bootstrap验收时bridge未运行、CAN未修改；随后在单独授权下完成CAN activation与bridge启动，见下节。

PC2 evidence：

```text
/home/unitree/Workspace/baoquanc/evidence/pc2_bootstrap_20260824_after_install.log
```

PC2机器自带配置优先。本机既有focal nginx active并监听80端口，APT记录其缺少Jammy不可用的`libssl1.1`；本次未删除/替换nginx、未运行`apt --fix-broken`、未升级系统包，而是以离线`dpkg`只安装明确Docker/can-utils包。

## 获批完成的 CAN 与实时 joint state

- `can0`按USB path`1-6:1.0`配置为`can_piper`、1 Mbit/s、`UP/ERROR-ACTIVE`，无bus error；
- 3秒candump收到9,087帧PiPER feedback；bridge启动前TX为0；
- bridge最终启动并持续运行，command gate关闭、`hardware_stop_required=false`、`arm_status=0`；
- SDK连接时`PiperInit()`发送13个limit/firmware查询帧，不是motion command；
- `/piper/joint_states`含`arm_j1..arm_j6`、6维position/velocity，实测50.000 Hz；SDK joint/status feedback均200 Hz；
- m45同次`/lowstate`约1052.7 Hz，ros-readonly Gate PASS。

2026-08-24 21:05 HKT，现场操作员在bridge `command gate closed`、未调用`enable/resume`且未发布`/piper/joint_command`时逐一人工移动PiPER六个关节，确认`arm_j1..arm_j6`的position分别随对应实体关节实时变化，PiPER关节映射全部PASS。同场也使用A2只读`joints-live`确认前12轴raw index/label映射全部PASS。该观察不包含命令方向、zero、hardware limits或stop/recovery验证。

Evidence：

```text
/home/unitree/Workspace/baoquanc/evidence/can_feedback_before_bridge_20260824.log
/home/unitree/Workspace/baoquanc/evidence/bridge_start_command_gate_closed_typing_fix_20260824.log
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/ros-readonly/20260824_205245_59854
```

首次两次bridge启动分别暴露ROS setup nounset与旧`typing_extensions`缺少`Self`；Docker entrypoint与image依赖已修复，最终SDK import和实机bridge startup均PASS。

## ROS 2 与已有A2 graph

PC2 login shell：

```text
ROS_DISTRO=humble
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ROS_DOMAIN_ID=UNSET  # shell未显式设置；默认domain 0 graph可见
ros-humble-ros-base 0.10.0
ros-humble-rmw-cyclonedds-cpp 1.3.4
```

M45的domain 0 container已只读确认：

- `/lowstate`: `unitree_hg/msg/LowState`，publisher可见，实测约`1052.7 Hz`。
- `/lowcmd`: `unitree_hg/msg/LowCmd`；最初既有bare DDS publisher/subscriber可见，约1000 Hz active流量随后在m45确认为宇树`ai_sport`。经操作员授权调用MotionSwitcher ReleaseMode后mode为空，5秒`/lowcmd`计数为0。该动作没有改变PC2 CAN/bridge，也没有调用任何PiPER command service。
- `/lf/lowstate`同时暴露`unitree_go`与`unitree_hg`类型，只作为diagnostic，不改变Stage2 backend `/lowstate`。
- `/arm_Command`和`/arm_Feedback`是既有`unitree_arm/msg/ArmString` bare-DDS endpoints；它们不是本项目的PiPER semantic bridge，不能替代`/piper/*` contract。

Policy-host evidence：

```text
deploy/a2_piper_stage2/evidence/connected_a2_pc2_20260824/policy_host_read_only.log
deploy/a2_piper_stage2/evidence/a2_readonly_20260824/
```

## 当前停止点

当前停止点如下：

1. Docker、CAN feedback、command-gate-closed bridge与ros-readonly Gate已完成。
2. A2/PiPER人工逐关节只读mapping预观察：操作员确认全部PASS；正式`joint-validation`表与receipt未生成。
3. A2 `ai_sport`已受保护release且`/lowcmd`停止；physical与ros-readonly人工approval均已执行。
4. 调用`resume/enable/stop/disable`或发送任何arm target：未执行。

当前Gate顺序的下一项是A2 standalone baseline。

2026-08-24 22:02 HKT补充：Stage2 two-stage stop candidate已在m45完成build与isolated no-output验证。设计中第一次L2+B让PiPER回reset hold，第二次L2+B才调用`/piper/stop`；本记录时PC2仍未执行上述service或动作。Physical随后已由操作员批准，当前receipt下一项是ros-readonly人工approval。

2026-08-24 22:18 HKT补充：按krushell fork的`piper_set_mit.py`，PC2 bridge position command已定向改为`MotionCtrl_2(1,1,0,0xAD)`后接`JointCtrl`，即MOVE J MIT/high-follow，不是逐电机`JointMitCtrl`。候选image `a2-piper-pc2-bridge:mit-high-follow-20260824`已在PC2运行；diagnostics实测command gate closed和精确mode tuple，全局`/piper/joint_command`、A2`/lowcmd`均无消息，未调用enable/resume。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/mit-high-follow/20260824_221815
```

2026-08-24 22:43 HKT补充：A2 baseline已PASS并由操作员批准，当前下一Gate为PiPER standalone baseline。m45 image `a2-piper-stage2:piper-roundtrip-baseline-20260824`的实时只读预检已PASS：可读取当前joint state与MIT/high-follow diagnostics，`command_gate_open=false`，程序明确报告未发送command；尚未调用PiPER enable/resume/stop。计划动作是5秒到`[0,1.48,-0.63,-0.84,0,1.57] rad`、保持5秒、5秒回到启动实测位姿再stop。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/piper-roundtrip-preflight/20260824_224332
```

2026-08-24 23:19 HKT补充：后续定向修复了三项实机问题：启动实测位姿不再用training URDF范围拒绝；enable会反复发送`MotionCtrl_2(1,1,0,0xAD)`并等待`arm_status=0/ctrl_mode=1`稳定；target与return使用独立的`0.5/3.5 deg`验收。最终10秒前伸、5秒hold、10秒回程与stop全部完成，target/return最大误差分别为`0.478/2.743 deg`，PiPER baseline PASS并由操作员批准：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/piper-baseline/20260824_231903_98613
```

controller只读limit反馈是六轴当前flash角度均`[-180,180] deg`、最大速度均`0.300 rad/s`，PC2 evidence为`/home/unitree/Workspace/baoquanc/evidence/joint_limits_20260824_225436`。宽泛flash角度不替代PiPER机械范围；live site应采用官方SDK范围`J1 ±150°, J2 0..180°, J3 -170..0°, J4 ±100°, J5 ±70°, J6 ±120°`与现场反馈的交集，实测速度换算为50 Hz target-rate上限`0.006 rad/tick`。

2026-08-24 23:41 HKT补充：上述边界已写入Stage2完成版`config/site.yaml`，canonical live-both preflight在隔离dummy topics上验证PASS，明确未调用PiPER enable/resume且未发布joint command。Evidence：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/live-preflight-both/20260824_234059_105233
```

2026-08-24 23:48 HKT补充：policy host最终轨迹image `a2-piper-stage2:dual-goal-pitch-trajectory-20260824`重建PASS，新的isolated live-both preflight确认task-space goal `[0.4,1.0472,0] → [0.4,-1.2566,0] / 6 s`已加载。该goal不是PiPER joint angle；PC2端仍以官方J1–J6机械范围与`0.300 rad/s`controller反馈限制实际target。本次preflight未调用PC2 resume/enable/stop，也未发布`/piper/joint_command`：

```text
/home/baoquanc/Workspace/GeneralSim2Real/deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/live-preflight-both/20260824_234854_107096
```

Owner随后要求精简live操作。新候选`a2-piper-stage2:dual-triggered-trajectory-20260824`在第一次`A`锁存实测位置后，由direct node自动且仅一次调用PC2 `/piper/resume`再调用`/piper/enable`；第二次`A`后只保持轨迹起点，显式`stage2_gate.sh trajectory`才启动轨迹。该候选的隔离验证把resume/enable/command/stop全部重定向到dummy endpoints，未改变当前PC2 bridge状态；evidence为Stage2 session内`evidence/triggered-trajectory-preflight/20260825_000056_112019`。

2026-08-25 first-A实机尝试中，PC2 `/piper/resume`与`/piper/enable`均返回成功，但旧policy-host node没有切入init；操作员未按second A。随后正式stop清理残留container，PC2 diagnostics回到gate closed、hardware_stop_required false、ctrl_mode 0。ROS graph实测`/piper/joint_command`的PC2 subscriber为BEST_EFFORT，原Stage2 publisher同样BEST_EFFORT，因此不存在最初猜测的QoS不兼容。最终policy-host handover fix以enable成功响应作为fresh gate-open确认，PC2 bridge代码/image未修改；isolated验证evidence为Stage2 session内`evidence/handover-fix-preflight/20260825_000936_116144`。

第二次first-A实机尝试中，PC2 resume/enable与handover seed均成功，PiPER短暂动作，但约263 ms后diagnostics报告command gate关闭，A2未进入init。操作员在second A前`Ctrl+C`，formal stop完成PiPER stop与A2 zero-LowCmd并清理live container，evidence为Stage2 `evidence/live-both/20260825_001224_116559`和`evidence/stop/20260825_001300_117022`。根因是policy host的single-threaded ROS executor在约1 kHz A2 LowState及PiPER callbacks下饿死50 Hz control timer，越过PC2 bridge的200 ms command watchdog；PC2 bridge、CAN、MIT/high-follow配置与command QoS均不需要修改。00:19 HKT policy host只读复核mode为空、5秒`/lowcmd`计数0且无live container，也排除A2官方mode未release。最终policy-host image `a2-piper-stage2:dual-triggered-executorfix-20260825`使用独立control callback group与2-thread executor，真实state command-remapped 8秒shadow PASS，evidence为Stage2 `evidence/executor-shadow-preflight/20260825_001628_118549`。

第三次live中A2 first-A init成功，但PiPER再次quick-stop。PC2 clock实测比m45慢约36.5秒；校正后`docker logs`中的`motion stopped: arm_status_5`与m45 enable后约0.15秒对齐，SDK枚举明确将5定义为joint communication abnormal。当前read-only SocketCAN `0x2A1`五帧均为`0000010000000000`，即ctrl_mode 0、arm_status 0、mode MOVE J、err_code 0，故障为enable切换瞬态而非持续CAN故障。原adapter仅用3个200 Hz正常样本确认mode ready；新image `a2-piper-pc2-bridge:stable-enable-20260825`要求连续0.5秒`arm_status=0 && ctrl_mode=1`后才返回enable成功，运行期间的非零status仍保持立即quick-stop。command-gate-closed重启后diagnostics实测arm_status/ctrl_mode=`0/0`、joint/status 200 Hz、feedback age约0.45 ms、hardware_stop_required false。

Policy host同时改为1秒actor-output handover及14秒`init→start→end→init`轨迹；该改动不改变PC2 command contract或0.006 rad/tick joint limit。真实state no-output evidence为Stage2 `evidence/roundtrip-handover-preflight/20260825_002957_122308`。

第四次first-A中stable-enable返回成功，但PC2随后记录`command_timeout`；policy host未进入init，操作员未按second A并formal stop。根因不是新的CAN fault，而是PC2的单线程enable callback为满足0.5秒稳定窗口而暂停joint-state发布约1秒，policy host在service完成前把PiPER state空窗按普通200 ms stale fault处理并清除了first-A lifecycle。PC2继续保留0.5秒stable-enable和运行期立即quick-stop；policy host `a2-piper-stage2:handover-stale-fix-20260825`只在enable in-flight phase保持锁存hold，正常运行期的state timeout不变。0.70秒dummy PC2阻塞隔离复现PASS evidence为Stage2 `evidence/handover-stale-preflight/20260825_004327_128268`。

2026-08-25 00:51 HKT最终dual live成功：first-A resume/enable与同步init、second-A PolicyActive、两次显式14秒arm-goal轨迹均完成；evidence为Stage2 `evidence/live-both/20260825_004737_129916`。第一次L2+B回manifest reset成功；第二次L2+B让A2趴地，但旧代码立即quick-stop PiPER，因此PiPER没有回first-A启动休息位。新policy-host image `a2-piper-stage2:manual-arm-rest-stop-20260825`已build并改为回程完成后再stop，尚待下一次实机验证；PC2 bridge保持`stable-enable-20260825`不变。

本轮formal stop evidence为Stage2 `evidence/stop/20260825_005119_130865`；随后A2恢复`ai_sport`，PC2 `runtime/bridge_ctl.sh stop`已执行，bridge status为空。下次从m45使用专用key连接`unitree@192.168.123.162`，工作目录固定为`/home/unitree/Workspace/baoquanc`。
