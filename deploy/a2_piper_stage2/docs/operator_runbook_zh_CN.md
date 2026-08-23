# A2 + PiPER Stage2 新手操作员唯一 Runbook

本文件是首次部署与日常 bring-up 的唯一执行入口。不要跳 Gate，不要把某一步的“预期 PASS”当成实际 PASS。每条 `stage2_gate.sh` 命令都会在 `.stage2_sessions/<session>/` 留下 receipt 和 stdout；命令失败后停在原 Gate，把 evidence 目录交给开发者。

Stage2 采用与本机 `main` locomotion 成功案例相同的 A2 direct 模式：C++ node 在同一进程内读取 `A2LowLevelInterface::latest_state()`，使用既有 mapping、PD 和 `publish_joint_commands()`，不需要新增 `/a2/joint_states` 或 `/a2/joint_command`。Python external-semantic transport 仍不可用，也不是本 Runbook 的 live 路径。

## 0. 先认清三台机器和三个终端

| 标记 | 机器/终端 | 用途 |
| --- | --- | --- |
| `P1` | Ubuntu 22.04 policy host 主终端 | 安装、配置、Gate、前台运行 Stage2 |
| `P2` | 同一 policy host 第二终端 | 看 status、调用明确获批的 PiPER service、执行 stop |
| `PC2` | A2 PC2 | 保持唯一 PiPER USB-CAN/bridge owner；只按既有 PC2 流程启动 bridge |

现场还必须有两个人：操作员拿 A2 remote；急停员只盯机械和 physical E-stop。任何人喊停都立即停止。

以下命令假定仓库位于：

```bash
cd /path/to/AliengoSim2Real/deploy/a2_piper_stage2
```

如果实际路径不同，只改这一条 `cd`。后续都在该目录执行。

## 1. Ubuntu 与 Docker bootstrap（P1，无机器人输出）

确认目标机是 Ubuntu 22.04 x86_64：

```bash
uname -m
lsb_release -ds
```

预期：`x86_64`，Ubuntu 22.04。否则停止。

安装官方 Docker apt repository、Engine 和 Compose v2：

```bash
./scripts/bootstrap_policy_host_ubuntu.sh
```

如果主机有`docker.io`、旧`docker-compose`、`containerd`等官方列明的冲突package，脚本会先列出并移除它们；它不删除`/var/lib/docker`中已有的image/container/volume。

精确 PASS：

```text
[PASS] Docker Engine and Compose v2 are installed; sudo hello-world succeeded.
```

首次安装后 logout/login 一次，再回到本目录运行：

```bash
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only
```

精确 PASS：

```text
[PASS] Docker Engine, Compose v2, daemon access, and hello-world are ready.
```

失败：不要手动混装 `docker.io`、旧 `docker-compose` 或改 daemon；保存完整终端输出后停止。

## 2. Offline 配置、image 和真实 bundle parity（P1，无机器人输出）

首次创建 `.env`。此时使用 mock site，不改有线网：

```bash
./scripts/configure_policy_host.sh \
  --iface enp131s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.mock.yaml"
```

精确 PASS 前缀：

```text
[PASS] wrote .../docker/.env
```

`enp131s0`、`192.168.123.222/24` 和 domain `0` 是同一台 A2 已有 evidence 对应的 site default，每次仍须在后面的 network Gate 复核。

创建本次部署 session：

```bash
./scripts/stage2_gate.sh init --operator <你的名字>
./scripts/stage2_gate.sh offline
```

`offline` 会依次运行 host check、CPU image build、bundle validator、manifest parity、benchmark 和 mock shadow。最后必须看到：

```text
PASS: offline
evidence: .../.stage2_sessions/<id>/evidence/offline/...
```

同时 `run_shadow.sh` 应出现：

```text
[PASS] shadow sequence completed; evidence: ...
```

检查 evidence 后才签字：

```bash
./scripts/stage2_gate.sh approve --gate offline --operator <你的名字>
./scripts/stage2_gate.sh next
```

这里确认的是模型/部署一致性。用户已确认 dual policy 完成 sim2sim，约 `0.039 m` 末态 arm position error 是 policy 效果 evidence，不是 deployment blocker；它也不能替代下面的硬件 mapping、watchdog 和逐 Gate 验收。

## 3. 切换到现场 direct site（P1，仍无机器人输出）

创建现场配置：

```bash
cp config/site.template.yaml config/site.yaml
```

把 compose 改为使用该文件；以下命令默认只写 `.env`，不改 host IP：

```bash
./scripts/configure_policy_host.sh \
  --iface enp131s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.yaml" \
  --force
```

如果 `ip -4 addr show dev enp131s0` 尚未显示 `192.168.123.222/24`，经现场网络负责人确认后才显式运行：

```bash
./scripts/configure_policy_host.sh \
  --iface enp131s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.yaml" \
  --apply-network \
  --force
```

预期：

```text
[PASS] host network applied: enp131s0 192.168.123.222/24
[PASS] wrote .../docker/.env
```

不要把 `--apply-network` 用在 Wi-Fi、管理口或名称不一致的 NIC 上。这个 `ip addr replace` 只对当前启动周期有效；policy host 重启后，从本节重新执行并再跑 Network Gate。

## 4. Network Gate（P1，只读）

```bash
./scripts/check_policy_host.sh --connected
./scripts/probe_policy_host_read_only.sh --session first_connected
./scripts/stage2_gate.sh network \
  --iface enp131s0 \
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
./scripts/stage2_gate.sh approve --gate network --operator <你的名字>
```

## 5. PC2 和 ROS read-only Gate（PC2 + P1，无命令发布）

PC2 先按已经验证的流程启动唯一 PiPER bridge。禁止同时启动第二个 SDK demo、`piper_ros` 或另一个 CAN owner。

在 P1 采集 PC2 evidence；把 SSH 地址换成真实值：

```bash
./scripts/probe_pc2_read_only.sh --ssh <user>@192.168.123.162 --session first_pc2
./scripts/probe_ros_graph_read_only.sh --session first_ros_graph
./scripts/stage2_gate.sh ros-readonly --duration 10
```

精确 Gate PASS：

```text
PASS: ros-readonly
```

必须实际看到：

- `/lowstate` 类型 `unitree_hg/msg/LowState`；
- `/lowcmd` 类型 `unitree_hg/msg/LowCmd`；
- `/piper/joint_states` 类型 `sensor_msgs/msg/JointState`；
- PiPER names 同时包含 `arm_j1` 到 `arm_j6`；
- A2 和 PiPER state 都持续有频率输出。

失败：检查 PC2 bridge、ROS domain、NIC 和 DDS；不要自动发现并改 topic 名。

## 6. Gate 0 人工现场批准（P1 receipt，无软件动作）

逐项口头确认并现场观察：A2 已支撑、PiPER 工作区清空、physical E-stop 可达、急停员就位、A2 remote 的 `Select` 与 `L2+B` 已知、只有一个 LowCmd owner、只有一个 PiPER CAN owner。

确认后记录：

```bash
./scripts/stage2_gate.sh approve --gate physical --operator <你的名字>
./scripts/stage2_gate.sh approve --gate ros-readonly --operator <你的名字>
```

## 7. A2 既有 locomotion baseline（P1 + remote，真实 A2 动作）

这一 Gate 不运行 Stage2；它原样复用同一台A2已跑通的`a2_real_robot_test.sh`，不会重写A2 mapping/LowCmd逻辑。成功路径依次执行connected/LowState/joint/no-lowcmd、MotionSwitcher check/release、既有policy、结束后的no-lowcmd和MotionSwitcher restore，全部输出进入同一evidence目录。

急停员 ready 后，在 P1 执行：

```bash
STAGE2_ALLOW_A2_BASELINE=1 \
./scripts/stage2_gate.sh a2-baseline \
  --iface enp131s0 \
  --duration 120 \
  --live \
  --operator <你的名字>
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
./scripts/stage2_gate.sh approve --gate a2-baseline --operator <你的名字>
```

如果baseline中途FAIL，脚本会立即停止，可能尚未来得及restore MotionSwitcher。不要直接重跑；先按A2既有流程确认无LowCmd publisher、机器人已安全落地，再由现场负责人用`a2_real_robot_test.sh motion-check/motion-restore`恢复原mode。失败evidence会保留具体停在哪一步。

## 8. PiPER 既有 bridge baseline（P1 + PC2，真实 PiPER enable/hold/stop）

该命令读取当前姿态、enable、持续发送相同姿态，最后调用 bridge stop；不会调用 resume，也不发送零姿态：

```bash
STAGE2_ALLOW_PIPER_BASELINE=1 \
./scripts/stage2_gate.sh piper-baseline \
  --live \
  --operator <你的名字>
```

预期包含：

```text
move smoke passed
PASS: piper-baseline
```

如果 bridge 已 fault-latched，命令应失败。只有 PC2 owner查清 fault 后才可明确决定是否调用 `/piper/resume`；Gate script不会自动 resume。

现场确认 hold-current、feedback、stop 后签字：

```bash
./scripts/stage2_gate.sh approve --gate piper-baseline --operator <你的名字>
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

同时 A2 existing `no-lowcmd` observer 必须 PASS，PiPER command observer必须完整超时且没有收到 `/piper/joint_command`。

```bash
./scripts/stage2_gate.sh approve --gate dry-run --operator <你的名字>
```

## 10. Gate 8 逐关节观察与人工验收（P1 observer + P2/现场负责人）

Gate 8不能由Stage2脚本自动移动关节：此时hardware-certified joint limits、方向和zero仍未知，自动构造target会反过来猜测本Gate要验证的事实。`joint-observe`自身只订阅状态，绝不发布A2 LowCmd或PiPER command。

开始前必须满足：A2稳定支撑、PiPER工作区隔离、physical E-stop operator就位；P2/现场负责人已经有各控制域原本就存在、已经单独批准的单关节程序。若某个关节没有这种程序，停在Gate 8，不要临时编写target，也不要用dual-policy node代替。

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
./scripts/stage2_gate.sh approve --gate joint-validation --operator <你的名字>
```

该approval要求当前session已有`joint-observe` PASS，并在receipt中明确记录逐关节表的路径，以及mapping、direction、unit、zero、limits和stop result均已人工审阅。脚本会拒绝行数不是18或direction/unit/zero/lower/upper/stop/reviewer任一必填列为空的表；未完成全部行时不要执行approve。随后运行：

```bash
./scripts/stage2_gate.sh next
```

预期下一步才是`fault --scenario process-stop`。

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
./scripts/stage2_gate.sh approve --gate fault --operator <你的名字>
```

## 12. 十分钟 coupled shadow（P1，无命令输出）

```bash
./scripts/stage2_gate.sh shadow --duration 600
```

不要中途 `Ctrl+C`。精确 PASS：

```text
PASS: shadow
```

检查 status：持续为 `contract=verified;mode=shadow;state=ready`，`a2_age_ms <= 200`、`piper_age_ms <= 200`、`skew_ms <= 50`，没有 `state=blocked`，并且 A2/PiPER command observer都没有收到消息。

```bash
./scripts/stage2_gate.sh approve --gate shadow --operator <你的名字>
```

## 13. Live 前手动完成 site.yaml

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

## 14. Gate 11A dog-only live（P1/P2，真实 A2 动作）

先记录 component-specific approval：

```bash
./scripts/stage2_gate.sh approve --gate live-dog_only --operator <你的名字>
./scripts/stage2_gate.sh live-preflight --component dog_only --operator <你的名字>
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
./scripts/stage2_gate.sh stop --operator <你的名字> --reason "dog-only operator stop"
```

P1 启动真实路径：

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component dog_only \
  --live \
  --operator <你的名字>
```

在启动direct node前，`live`会原样调用A2 existing wrapper执行MotionSwitcher check/release；release失败时node不会启动。它不会自动restore，因为restore必须在确认A2已安全落地、没有LowCmd publisher后由operator决定。

操作：第一 `A` stand-up；站稳且摇杆归中后第二 `A`；只做低幅动作。`dog_only` 仍运行 arm actor生成 body plan，但不发布 PiPER command。status 的 PolicyActive 必须显示：

```text
component=dog_only
a2_output=dog_actor_target
piper_output=not_published
```

正常停止优先使用 `L2+B` controlled-down。异常立即用 `Select`；若 node/remote失效，P2 执行准备好的 `stage2_gate.sh stop`。physical E-stop始终优先于软件。

## 15. Gate 11B arm-only live（P1/P2，真实 A2 hold + PiPER 动作）

每个 component 都要独立 approval/preflight：

```bash
./scripts/stage2_gate.sh approve --gate live-arm_only --operator <你的名字>
./scripts/stage2_gate.sh live-preflight --component arm_only --operator <你的名字>
```

先在P1启动node：

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component arm_only \
  --live \
  --operator <你的名字>
```

此时不要按第二次`A`。第一次按`A`后，direct node开始A2 stand-up，并持续发布PiPER measured-position hold。P2先确认Stage2 status出现：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  policy-runtime timeout 10 ros2 topic echo --once /a2_piper_stage2/status --field data
```

必须包含：

```text
mode=live
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

必须看到`message: command gate open`，并在values里看到`key: command_gate_open`对应`value: 'true'`，且PiPER保持当前姿态。否则不要按第二次`A`，执行stop。只有A2站稳、PiPER hold稳定、摇杆归中，才按第二次`A`。`arm_only` 为保持base稳定仍向A2发布default-position PD hold，不是“A2完全无输出”。PolicyActive status必须显示：

```text
component=arm_only
a2_output=default_position_hold
piper_output=arm_actor_target
```

从静态 arm goal `[0.6, 0, 0]`、支撑/隔离、低幅开始。正常停止 `L2+B` 会请求 A2 controlled-down和 `/piper/stop`。

## 16. Gate 12 both live（支撑/夹具、无接触）

只有 dog-only 和 arm-only 各自验收后：

```bash
./scripts/stage2_gate.sh approve --gate live-both --operator <你的名字>
./scripts/stage2_gate.sh live-preflight --component both --operator <你的名字>
```

使用与arm-only完全相同的严格顺序：P1启动node → 第一次`A` → P2确认`piper_output=measured_position_hold` → P2读diagnostics → 仅在latched时显式resume → P2 enable → 再读diagnostics确认`command gate open`且hold稳定 → 摇杆归中 → 第二次`A`。

```bash
STAGE2_ALLOW_LIVE=1 \
./scripts/stage2_gate.sh live \
  --component both \
  --live \
  --operator <你的名字>
```

不要把resume/enable提前到第一次`A`之前；bridge要求enable后在`200 ms`内收到fresh command。Gate script不会替operator调用这两个service。Dog-only永远不resume、不enable PiPER。

PolicyActive status必须显示：

```text
component=both
a2_output=dog_actor_target
piper_output=arm_actor_target
```

Gate 12 只允许支撑/夹具、无任务接触。自由站立和任务接触分别属于 Gate 13/14，需要新的现场计划和批准，不由本脚本自动扩大动作范围。

## 17. Stop、状态与证据

随时查看当前 Gate：

```bash
./scripts/stage2_gate.sh status
./scripts/stage2_gate.sh next
```

软件 stop（P2）会中断当前 live container、调用 `/piper/stop`，再原样复用 A2 verified wrapper发布 zero LowCmd。A2 可能失去支撑力，因此正常情况先用 `L2+B` controlled-down；软件 stop只在 immediate stop 场景使用：

```bash
./scripts/stage2_gate.sh stop \
  --operator <你的名字> \
  --reason "写明原因"
```

精确 PASS：

```text
PASS: direct process interrupted, PiPER stop called, verified A2 zero-LowCmd path completed
```

如果这个 PASS 没出现，立即用 physical E-stop，不重复重试命令。

A2已经安全支撑/落地并确认没有LowCmd publisher后，现场负责人可显式恢复同一台A2原mode（默认`ai_sport`）：

```bash
docker compose --env-file docker/.env -f docker/compose.yaml run --rm --no-deps \
  -e A2_ALLOW_SELECT_MODE=1 \
  policy-runtime /opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh \
  motion-restore enp131s0
```

不要在A2仍站立、Stage2 container仍运行或`no-lowcmd`未通过时执行restore。

所有 session evidence 位于：

```text
deploy/a2_piper_stage2/.stage2_sessions/<id>/
├── session.receipt
├── approvals/
├── results/
└── evidence/<step>/<timestamp>/
```

每个receipt记录实际命令、时间、operator、退出码和evidence路径。不要删除失败evidence，也不要把新session的approval复制到旧session。
