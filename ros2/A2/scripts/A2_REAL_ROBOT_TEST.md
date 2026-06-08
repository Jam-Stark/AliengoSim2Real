# A2 Real Robot Validation Guide

本文档面向部署机 operator。前提是 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md`
中的无 robot Docker virtual tests 已通过，且即将连接真实 A2。部署机 workspace：

```text
/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
```

本文所有 connected tests 默认只 observe。任何会 publish configured LowCmd topic
（默认 `/lowcmd`）或释放内置运动服务的步骤，都需要显式 env guard。

## 0. Scope and Safety Boundary

本流程连接 real A2，但默认不做 motion。

硬性边界：

- `connected-preflight`、`no-lowcmd`、`lowstate`、`joints-live`、`joints`、`remote-live`、`remote`、`smoke-remote`、`motion-check`、`policy-listen-remote`、`policy-aux-live`、`policy-aux-monitor` 默认不发布 configured LowCmd topic message。
- `motion-release` 会调用 Unitree `MotionSwitcherClient::ReleaseMode()`，必须显式设置 `A2_ALLOW_RELEASE_MODE=1`。
- `motion-select` / `motion-restore` 会调用 Unitree `MotionSwitcherClient::SelectMode()` 恢复内置 motion service，必须先停止 policy/LowCmd publisher、重新运行 `no-lowcmd` pass，并显式设置 `A2_ALLOW_SELECT_MODE=1`。
- `zero-lowcmd` 会发布 zero `LowCmd`，必须显式设置 `A2_ALLOW_ZERO_LOWCMD=1`。
- `policy-enable-remote` 是最后阶段，会设置 `enable_motion:=true`；first `A` 才开始 stand-up/hold `LowCmd`，second `A` 才进入 warmup/policy handover，必须显式设置 `A2_ALLOW_ENABLE_MOTION=1`。
- 不运行 `stand_test`。
- 不使用 `192.168.124.x` 做 SDK development；A2 SDK development 只走 `192.168.123.0/24`。

进入任何 publish path 前必须满足：

- A2 离地、绑扎、限功率或处于其他可控测试状态。
- 周围清空，operator 手边有 hardware emergency stop。
- `ai_sports` / `ai_sport` 已确认关闭或准备通过 `motion-release` 关闭。
- `no-lowcmd` observe-only check 已确认 configured LowCmd topic（默认 `/lowcmd`）当前没有 active command messages。
- 现场只保留一个低层控制 publisher，不要并行运行其他 configured LowCmd topic
  publisher（默认 `/lowcmd`）。

恢复 Unitree 内置 motion service 前必须满足：

- 已停止 `a2_policy_deploy`、`a2_lowlevel_smoke publish_zero` 或任何其他 LowCmd publisher。
- 已重新运行 `A2/scripts/a2_real_robot_test.sh no-lowcmd 5`，并看到 pass。
- 只使用 guarded `motion-select` / `motion-restore` 或 Unitree App 恢复；不要在 LowCmd publisher 仍 active 时恢复内置 motion service。

重要说明：当前 `a2_policy_deploy command_source=remote` 已取消 `L2` locomotion gate；
PolicyActive 中 valid sticks 会在 deadzone 后直接映射 locomotion command。`enable_motion=true`
下 first `A` 后 stand-up/holder 会持续发布 standing joint targets，second `A` handover 后
policy 也可能继续发布 standing joint targets。因此 `enable_motion=true` 始终属于 motion
path。`Select` 是 primary local stop；`L2+B` 只保留为附加 stop path，因为 A2 R3 `L2`
decode 曾出现不可靠。

## 1. Host Network and Docker Entry

Host 上执行，`enp131s0` 是当前部署机报告中的 A2 Ethernet NIC。用户/operator 自己负责确认 NIC 名称和 IP 配置。

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
sudo ip link set enp131s0 up
sudo ip addr flush dev enp131s0
sudo ip addr add 192.168.123.99/24 dev enp131s0
ip -4 addr show enp131s0
ping -c 5 192.168.123.161
```

Ideal result：

- `ip -4 addr show enp131s0` 显示 `192.168.123.99/24`。
- `ping -c 5 192.168.123.161` 有 reply，packet loss 接近 `0%`。

进入 Docker container：

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
A2_NET_IFACE=enp131s0 bash ros2/A2/docker/run_container.sh bash
```

Ideal result：

- container entrypoint 显示 `A2_NET_IFACE=enp131s0`。
- `CYCLONEDDS_URI` 指向 `NetworkInterface name="enp131s0"`。
- 当前目录是 `/work/projects/AliengoSim2Real/ros2`。

确认 A2 package 已 build：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 pkg prefix a2_lowlevel
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_lowlevel_smoke
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_policy_deploy
```

Ideal result：

- `ros2 pkg prefix a2_lowlevel` 返回 `/work/projects/AliengoSim2Real/ros2/install/a2_lowlevel`。
- 两个 `test -x` 都 exit `0`。

如 container 里还没 build：

```bash
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
```

## 2. Connected Preflight and Topic Visibility

Topic naming 说明：

- A2_Guide / Unitree DDS reference names 是 `rt/lowstate` / `rt/lowcmd`。
- 当前部署机 real A2 ROS2 graph 观测到的 visible topics 是 `/lowstate`、`/lowcmd`，
  同时还能看到 `/lowstate_raw`、`/lf/lowstate`、`/wirelesscontroller` 等。
- 本仓库 ROS2 backend 和 real-robot validation script 默认使用 `/lowstate` / `/lowcmd`。
  `A2LowLevelInterface` 参数名是 `lowstate_topic` / `lowcmd_topic`。
- 当前 preflight result 中 `/lf/lowstate` 输出 type 为
  `['unitree_go/msg/LowState', 'unitree_hg/msg/LowState']`，存在 type ambiguity；它只作为
  diagnostic info，不作为默认 A2 policy / lowlevel backend topic。默认继续使用
  `/lowstate`。
- 如现场 ROS2 graph 不同，可用 env 切换：

```bash
A2_LOWSTATE_TOPIC=/rt/lowstate A2_LOWCMD_TOPIC=/rt/lowcmd \
A2/scripts/a2_real_robot_test.sh connected-preflight enp131s0
```

在 container 内执行：

```bash
A2/scripts/a2_real_robot_test.sh connected-preflight enp131s0
```

Ideal result：

- log 写入 `/tmp/a2_real_robot_tests/connected_preflight_*.log`。
- 输出 `ROS_DISTRO=humble`、`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`。
- `A2_NET_IFACE=enp131s0`。
- `A2_LOWSTATE_TOPIC=/lowstate`、`A2_LOWCMD_TOPIC=/lowcmd`，除非 operator 显式覆盖。
- `ping -c 5 192.168.123.161` 成功。
- `ros2 topic list` 能看到 configured lowstate / lowcmd topics；默认检查 `/lowstate` 和 `/lowcmd`，
  缺失时 fail。
- preflight 会在 topic 存在时打印 configured lowstate topic、`/lf/lowstate` diagnostic
  topic、configured lowcmd topic 的 `ros2 topic info -v`，用于确认 publisher/subscriber
  endpoint。
- preflight 会检查 configured lowstate topic type 包含 `unitree_hg/msg/LowState`、
  configured lowcmd topic type 包含 `unitree_hg/msg/LowCmd`。
- `/lowcmd` 出现 Publisher / Subscription endpoint 不等于一定有 active command messages；
  endpoint 只能说明 DDS graph 中存在 endpoint。
- `ros2 interface show unitree_hg/msg/LowState` 和 `LowCmd` 正常输出。
- 最后一行输出
  `PASS: connected preflight topics visible and types match configured A2 backend topics`。

失败时先不要运行任何 publish path；回传 preflight log。

## 3. No-LowCmd Traffic Observe-Only Check

进入任何 publish path 前，先确认 configured LowCmd topic（默认 `/lowcmd`）没有 active
command traffic。本步骤只订阅 configured LowCmd topic，不发布任何消息。

```bash
A2/scripts/a2_real_robot_test.sh no-lowcmd 5
```

Ideal result：

- log 写入 `/tmp/a2_real_robot_tests/no_lowcmd_*.log`。
- 输出 `lowcmd_topic=/lowcmd`，或显示 operator 配置的 `A2_LOWCMD_TOPIC`。
- `lowcmd_count=0`。
- 最后一行 `PASS: no /lowcmd messages observed`。

如果 `no-lowcmd` 收到任何 message，说明已有 LowCmd publisher 正在发送 active command
traffic。必须先停止该 publisher，重新运行 `no-lowcmd` 直到 pass；不要进入
`zero-lowcmd`、`policy-enable-remote` 或其他任何 publish path。

## 4. Continuous LowState Rate / Tick / Freshness

观察真实 robot 是否持续发布 configured lowstate topic（默认 `/lowstate`）：

```bash
A2/scripts/a2_real_robot_test.sh lowstate 15
```

Ideal result：

- `lowstate_count` 大于 0。
- `lowstate_rate_hz` >= `50.00`。
- `max_interarrival_gap_ms` <= `250.00`。
- `mode_pr_values`、`mode_machine_values` 有稳定值。
- `tick_delta_ms` 有统计输出；`tick` 是 Unitree 1ms counter。
- `nonfinite_message_count=0`。
- 最后一行 `PASS: lowstate freshness/rate/finiteness checks passed`。

可通过 env 调整阈值：

```bash
A2_LOWSTATE_MIN_HZ=50 A2_LOWSTATE_MAX_GAP_MS=250 \
A2/scripts/a2_real_robot_test.sh lowstate 15
```

## 5. Joint State Mapping / Direction Observe-Only

本步骤只订阅 configured lowstate topic（默认 `/lowstate`），不创建 LowCmd publisher，
不做任何 control。目的
是在进入 remote、MotionSwitcher release、zero `LowCmd` 或 policy 之前，先确认真实
A2 的 first-12 `motor_state` 对应当前 low-level joint order 和 sign direction 假设。

固定 A2 low-level joint labels：

```text
00 FR_BODY   01 FR_THIGH   02 FR_CALF
03 FL_BODY   04 FL_THIGH   05 FL_CALF
06 RR_BODY   07 RR_THIGH   08 RR_CALF
09 RL_BODY   10 RL_THIGH   11 RL_CALF
```

推荐先运行 live table。默认 duration 是 `0`，表示一直运行直到 Ctrl-C；下面命令
每 `0.2s` 刷新一次终端，只订阅 configured lowstate topic，不发布 LowCmd：

```bash
A2/scripts/a2_real_robot_test.sh joints-live
```

也可以限制运行秒数：

```bash
A2_LIVE_PRINT_PERIOD=0.2 \
A2_JOINT_MIN_DELTA=0.03 \
A2/scripts/a2_real_robot_test.sh joints-live 30
```

Ideal live result：

- 输出 `joints_live observe_only_no_lowcmd_publish=True`。
- live table 固定显示 `00..11` 的 `FR_BODY` 到 `RL_CALF`。
- 每一行都有当前 `q`、`dq`、`delta_from_start` 和本次运行以来的 `range`。
- `chg` 列中 `*` 表示该 joint 的当前 delta 或 observed range 超过
  `A2_JOINT_MIN_DELTA`，默认 `0.03 rad`。
- 手动只移动一个 joint 时，对应 label 的 `q` / `delta_from_start` / `range` 应连续变化，
  其他 joint 应保持接近不变。
- 如果没有收到 lowstate、`motor_state` 少于 12 个、或 first-12 `q/dq` 出现 NaN/Inf，
  live table 会实时显示 `WAIT` / `WARN`。

如果不希望清屏，改成滚动输出：

```bash
A2_LIVE_CLEAR_SCREEN=0 A2/scripts/a2_real_robot_test.sh joints-live 30
```

旧的 run-end summary / CSV validation 仍可运行：

```bash
A2/scripts/a2_real_robot_test.sh joints 15
```

旧 `joints` ideal result：

- 输出 `joints_observe_only_no_lowcmd_publish=True`。
- `lowstate_count` 和 `valid_joint_sample_count` 大于 0。
- `motor_state_lengths_seen` 至少为 `12`；如果 generated sequence 大于 `12`，脚本只读取前 `12` 个。
- `short_motor_state_count=0`。
- `nonfinite_joint_message_count=0`。
- 输出 `joint_sample` 周期性快照，每个 label 都有 `q`、`dq`、`delta_from_start`。
- 输出 `joint_summary_by_range_desc`，包含每个 joint 的 `start/end/min/max/range/max_abs_dq`。
- 最后一行 `PASS: joint mapping/direction observe-only checks passed; no lowcmd was published`。

逐关节手动验证建议每次只移动一个关节的小角度。仅在 robot 已安全支撑、operator
确认该状态允许手动移动时执行；如果现场条件不允许移动，静态记录不会 fail，但无法
完成 mapping/direction 判定。

推荐逐个关节运行，每次把 CSV 文件名写清楚：

```bash
A2_LIVE_PRINT_PERIOD=0.2 \
A2_JOINT_MIN_DELTA=0.03 \
A2/scripts/a2_real_robot_test.sh joints-live 30
```

需要 CSV time series 时，再跑旧 summary tool：

```bash
A2_JOINT_PRINT_PERIOD=0.25 \
A2_JOINT_MIN_DELTA=0.03 \
A2_JOINT_CSV=/tmp/a2_real_robot_tests/joints_FR_BODY.csv \
A2/scripts/a2_real_robot_test.sh joints 20
```

如何判断 joint mapping：

- 只移动一个目标关节时，`joints-live` 中理想情况下只有目标 label 的 `q`、`delta_from_start`
  和 `range` 连续变化，并被 `*` 标记；旧 `joints` 的 `candidate_changed_joints`
  理想情况下只出现目标 label。
- 如果另一个 label 的 `range` 最大或超过 `A2_JOINT_MIN_DELTA`，说明 `motor_state`
  index/order 和当前 label 假设不一致；不要进入任何 control path。
- 如果多个非目标 joint 明显超过阈值，先排除机械联动、支撑状态、operator 是否同时
  带动了相邻关节；无法解释时按 mapping mismatch 处理。
- 如果没有 joint 超过阈值，脚本会打印提示但不 fail；这通常表示测试期间没有足够
  movement，调小 `A2_JOINT_MIN_DELTA` 或重新逐关节移动验证。

如何判断 sign direction：

- 对每个 joint，沿着和 sim/training convention 中 `+q` 相同的物理方向做小角度
  movement，并观察目标 label 的 `delta_from_start` 正负号。
- 如果目标 joint 在物理 `+q` 方向移动时 `delta_from_start` 为正，当前 sign direction
  与 no-inversion 假设一致。
- 如果目标 joint 在物理 `+q` 方向移动时 `delta_from_start` 为负，记录该 joint 需要
  sign inversion；在修正 mapping/sign 前不要运行 `zero-lowcmd` 之后的 motion path。
- 对 BODY、THIGH、CALF 都按同一方法记录；最终需要形成 12 个 joint 的 order/sign
  validation 记录。

## 6. Remote Raw Decode and Smoke Logging

先用 live Python observer 验证 `wireless_remote[40]` raw layout。默认 duration 是
`0`，表示一直运行直到 Ctrl-C；该工具只订阅 configured lowstate topic，不发布 LowCmd。

```bash
A2/scripts/a2_real_robot_test.sh remote-live
```

测试期间移动 sticks，并按 `L2`、`A/B/Start/Select` 等按钮做变化。

Ideal live result：

- 输出 `remote_live observe_only_no_lowcmd_publish=True`。
- 每 `A2_LIVE_PRINT_PERIOD` 秒刷新当前 `raw` sticks 和经过 deadzone/clamp 后的
  `display` sticks。
- `valid=True`；如果 stick float 出现 NaN/Inf 或 packet length 不足，会实时显示
  `WARN` 和 `invalid_remote_count`。
- `pressed_buttons` 随当前按钮状态实时显示；即使 button state 不变，stick 数值也会
  按刷新周期持续打印。
- 移动 `lx/rx/ry/ly` 时，终端对应 raw/display 值应连续变化。

如果不希望清屏，改成滚动输出：

```bash
A2_LIVE_CLEAR_SCREEN=0 A2/scripts/a2_real_robot_test.sh remote-live 30
```

旧的 summary validation 仍可运行：

```bash
A2/scripts/a2_real_robot_test.sh remote 20
```

旧 `remote` ideal result：

- log 写入 `/tmp/a2_real_robot_tests/remote_*.log`。
- `remote_change` 行随按钮变化输出。
- stick raw range 中至少一个方向出现非零变化。
- `remote_invalid_count=0`。
- 最后一行 `PASS: remote decode checks passed`。

如果只想确认 packet 可解析、不要求移动遥控器：

```bash
A2_REMOTE_ALLOW_ZERO=1 A2/scripts/a2_real_robot_test.sh remote 10
```

再验证 C++ smoke node 的 `log_remote` 输出：

```bash
A2/scripts/a2_real_robot_test.sh smoke-remote 20
```

Ideal result：

- log 写入 `/tmp/a2_real_robot_tests/smoke_remote_*.log`。
- `a2_lowlevel_smoke` 打印 `tick=... mode_pr=... mode_machine=...`。
- `remote_sticks=[lx=..., rx=..., ry=..., ly=...] buttons=...` 随遥控器变化。
- `timeout` exit `124` 被脚本接受；该 node 正常 spin，不是 crash。
- 不发布 configured LowCmd topic（默认 `/lowcmd`）。

## 7. MotionSwitcher Check, Guarded Release, and Guarded Restore

官方 A2 low-level 文档要求在 low-level control 前关闭 Unitree 内置 motion service，当前服务名为 `ai_sports`；MotionSwitcher guide 也列出 `ai_sport`。先只做 check：

```bash
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

Ideal result：

- 脚本在 `/tmp` 编译临时 `MotionSwitcherClient` helper。
- helper compile 会打印全部 SDK2 include dirs / lib dirs。新版脚本会自动追加
  `install/include/ddscxx`、`install/include/ddsc`、`thirdparty/include/ddscxx`、
  `thirdparty/include/ddsc` 等 nested DDS header dirs，并追加 `install/lib`、
  `thirdparty/lib/$(uname -m)` 等 DDS lib dirs，链接 `-lddscxx -lddsc`。
- 输出 `CheckMode ret=0 form='...' name='...' service='...'`，其中 `service` 是按 Unitree SDK2
  sample 归一化后的 service name。
- 如果 `name` 非空，表示仍有内置 motion mode active。
- 此步骤不调用 `ReleaseMode`。

如果旧脚本遇到 `fatal error: dds/topic/TopicTraits.hpp: No such file or directory`，
说明 SDK2 DDS C++ headers 位于 nested `ddscxx` include dir。可在 container 内先确认：

```bash
find /opt/unitree/unitree_sdk2 -path '*/ddscxx/dds/topic/TopicTraits.hpp' -print
find /opt/unitree/unitree_sdk2 -path '*/libddsc*' -print
```

然后重新运行：

```bash
A2_MOTION_HELPER_SRC=/tmp/a2_motion_switcher_helper_test.cpp \
A2_MOTION_HELPER_BIN=/tmp/a2_motion_switcher_helper_test \
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

期望 log 中能看到类似 `/opt/unitree/unitree_sdk2/install/include/ddscxx` 或
`/opt/unitree/unitree_sdk2/thirdparty/include/ddscxx`，以及
`/opt/unitree/unitree_sdk2/install/lib` 或
`/opt/unitree/unitree_sdk2/thirdparty/lib/x86_64`。如果 compile 仍失败，脚本会停止，
不会继续执行不存在的 helper。

如果 helper 运行时出现 `free(): invalid pointer` 这类进程内 abort，优先检查 log 中的
`ldd with SDK2 LD_LIBRARY_PATH prefix`。因为 container 已 source ROS2 Humble，
`LD_LIBRARY_PATH` 里可能有 ROS2/CycloneDDS 的 `libddsc*.so`；新版脚本会生成 wrapper，
运行 helper 前把 SDK2 `install/lib` / `thirdparty/lib/$(uname -m)` 放到
`LD_LIBRARY_PATH` 最前面，并打印 helper 内部阶段：

- `ChannelFactory::Init(domain=0, iface='...')`
- `MotionSwitcherClient::Init()`
- `CheckMode()`
- `ChannelFactory::Release()`

如果仍 abort，把该段 log 回传，用阶段日志判断是 SDK2 DDS init、MotionSwitcher RPC init、
CheckMode call 还是 shutdown release 阶段的问题。

确认 robot 安全状态后，显式 release：

```bash
A2_ALLOW_RELEASE_MODE=1 A2/scripts/a2_real_robot_test.sh motion-release enp131s0
```

Ideal result：

- 输出每次 `Release attempt`。
- 每次都打印 `CheckMode ret=... form='...' name='...' service='...'` 和 `ReleaseMode ret=...`。
- 最终 `CheckMode` 的 `name=''`，并输出 `Motion mode released.` 或 `Motion mode already released.`。

如果 MotionSwitcher RPC 不可用，按官方文档可用 Unitree App 关闭内置运动服务；关闭后重新运行 `motion-check`，确认 `name=''`。

完成 low-level / policy 测试后，如用户已经停止 policy 和 LowCmd publisher，可通过 guarded
`SelectMode` 恢复 Unitree 内置 motion service。默认恢复 mode 是 `ai_sport`；如果现场确认
服务名不同，可用 `A2_MOTION_RESTORE_MODE` 或 `motion-select IFACE MODE` 显式指定。

部署机 container 内推荐顺序：

```bash
cd /work/projects/AliengoSim2Real/ros2
source install/setup.bash
A2/scripts/a2_real_robot_test.sh no-lowcmd 5
A2_ALLOW_SELECT_MODE=1 A2/scripts/a2_real_robot_test.sh motion-restore enp131s0
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

Ideal restore result：

- `no-lowcmd` 输出 `PASS: no /lowcmd messages observed`，或显示 operator 配置的 `A2_LOWCMD_TOPIC` 并 pass。
- `motion-restore` 输出 `SelectMode('ai_sport') ret=0`。
- `motion-restore` 的 after `CheckMode ret=0 form='0' name='ai' service='ai_sport'` 是 expected alias；
  如果现场 raw name 直接显示 `ai_sport` 也可接受。
- 随后的 `motion-check` 也显示 raw `name='ai'` / normalized `service='ai_sport'`，或 raw
  `name='ai_sport'`。

如果 `SelectMode` ret 非 `0`、after `CheckMode` 的 raw `name` 或 normalized `service` 都不是目标 mode，
或 MotionSwitcher RPC 不稳定，不要继续脚本恢复；使用 Unitree App fallback 恢复内置 motion service，
并再次运行 `motion-check` 确认当前 mode。

## 8. Guarded Zero-LowCmd CRC Validation

本步骤会 publish zero configured LowCmd topic（默认 `/lowcmd`），用于验证：

- `LowCmd.crc` 与 official raw layout CRC32 一致。
- `LowCmd.mode_machine` 跟随最新 `LowState.mode_machine`。
- 35 个 `MotorCmd` 全部为 STOP mode `0x00`，且 `q/dq/tau/kp/kd` 全零。

必须先完成 `lowstate` pass、`joints` order/sign 判定，以及 `motion-release` / App 关闭 motion service。

```bash
A2_ALLOW_ZERO_LOWCMD=1 A2/scripts/a2_real_robot_test.sh zero-lowcmd 8
```

Ideal result：

- observer log 写入 `/tmp/a2_real_robot_tests/zero_lowcmd_observer_*.log`。
- smoke log 写入 `/tmp/a2_real_robot_tests/zero_lowcmd_smoke_*.log`。
- `a2_lowlevel_smoke` warning 明确显示 `publish_zero will publish zero/stop LowCmd frames`。
- observer 至少收到一条 configured LowCmd topic message。
- 每条输出 `crc_ok=True zero_ok=True mode_ok=True`。
- 最后一行 `PASS: LowCmd CRC/zero/mode checks passed`。

失败时立即停止，不进入 policy motion path。

## 9. Policy Listen-Only Remote Gate

此步骤加载 policy、使用 real configured lowstate topic（默认 `/lowstate`）和 remote
command source，但 `enable_motion=false`，同时 observer 确认没有任何 configured
LowCmd topic message（默认 `/lowcmd`）。publish path 仍有 `enable_motion`、fresh-state、
history warmup、NaN/Inf、CRC/mode routing 等 guards。
即使 operator 在此阶段按 primary local stop `Select`，或按附加 stop path `L2+B` 请求
local stop，policy node 也只 reset runtime，不发布 zero LowCmd；no-lowcmd observer 会验证
这个 listen-only boundary。

```bash
A2/scripts/a2_real_robot_test.sh policy-listen-remote 20
```

Ideal result：

- policy log 写入 `/tmp/a2_real_robot_tests/policy_listen_remote_*.log`。
- observer log 写入 `/tmp/a2_real_robot_tests/policy_listen_no_lowcmd_*.log`。
- policy 输出 `Validated A2 policy contract...`。
- policy 输出 `enable_motion=false` 和 `command_source=remote`。
- 默认 run config 会传入 `publish_aux_debug=true`，因此 policy history warmup 后会执行
  inference-only 并发布 `/a2/policy_aux`，但仍然不会发布 configured LowCmd topic。
- 按 `Select` 时 policy 只记录 local stop / runtime reset，不发布 zero LowCmd；`L2+B`
  仍可作为附加 stop path，但现场 primary stop 是 `Select`。
- no-lowcmd observer 会以 `policy duration + 2s` 运行，覆盖完整 policy runtime 和收尾窗口。
- no-lowcmd observer 输出 `PASS: no /lowcmd messages observed`，或显示 operator 配置的
  `A2_LOWCMD_TOPIC`。

如果此步骤观察到任何 configured LowCmd topic message，不要继续。

`policy-aux-live` 是 independent listen-only / no-lowcmd smoke。它运行
`a2_policy_deploy`，设置 `enable_motion=false`、`command_source=remote`、
`monitor_policy_aux=true`，并同时启动 `no-lowcmd` observer 覆盖完整 duration。默认
duration 是 `0`，表示持续运行直到 Ctrl-C；finite duration 时 observer 会额外覆盖收尾窗口。

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-live
```

或限制运行时间：

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-live 30
```

Ideal aux monitor result：

- policy log 写入 `/tmp/a2_real_robot_tests/policy_aux_live_*.log`。
- no-lowcmd observer log 写入 `/tmp/a2_real_robot_tests/policy_aux_live_no_lowcmd_*.log`。
- wrapper 会读取 `A2/config/a2_policy_remote.env`，并打印 effective remote caps、deadzone、
  `require_standup_before_policy`、`policy_aux_expected_dim`、print period 和
  standing/walking hysteresis thresholds。
- policy 输出 `enable_motion=false`、`command_source=remote`、`monitor_policy_aux=true`。
- history warm 后 policy 会执行 inference 只用于监测，不发布 LowCmd。
- aux 输出默认按 Aliengo convention 解释 dim 6：
  `pred_base_lin_vel[0..2]` 和 `pred_base_force_local[0..2]`。如果 aux 为空，说明模型可能
  没有返回 `tuple[1]`；如果 dim 不是 expected dim，会标记 layout unverified 并打印前
  最多 8 个值。
- aux monitor 的 history / gait 是由当前 lowstate、remote command 和 policy action obs
  独立滚动估计；它不是 robot base velocity/force 的外部传感器测量。
- standing/walking gate 在 listen-only monitor 中也只影响 policy gait clock：raw requested
  command 非 standing 时 `command_walking`；raw requested command standing 时按 aux
  `force_xy=hypot(aux[3], aux[4])` hysteresis，enter `0.2`、exit `0.05`。aux 只有
  inference 后才有，因此 force-derived gate mode 影响下一轮 observation。
- no-lowcmd observer 必须输出 `PASS: no /lowcmd messages observed`，或显示 operator
  配置的 `A2_LOWCMD_TOPIC` 并 pass。

如果 aux 中出现 NaN/Inf、dim/layout 不符合预期，或者 no-lowcmd observer 失败，不要继续
进入 real policy motion；先保存 logs 并确认 policy aux layout。当前 run config 已启用
brake gate，`policy-aux-live` 仍是 `enable_motion=false` listen-only path，因此即使传入
brake 参数也不会发布 LowCmd。

`policy-aux-monitor` 是 active policy aux topic subscriber。它只订阅
`std_msgs/msg/Float32MultiArray` topic，不启动 policy node、不启动 no-lowcmd observer、
不发布 LowCmd。它用于另一个 Docker terminal 实时观察正在运行的 `policy-enable-remote`
发出的 `/a2/policy_aux`，也可以观察 `policy-listen-remote` 在 `enable_motion=false`
下发布的 same aux debug topic：

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-monitor 0
```

如果需要确认 topic publisher/type，另一个 terminal 可执行：

```bash
ros2 topic info /a2/policy_aux -v
```

Ideal active-topic monitor result：

- wrapper 打印 `publish_aux_debug`、`aux_debug_topic`、`aux_expected_dim` 和 print period。
- `ros2 topic info /a2/policy_aux -v` 显示 type 是
  `std_msgs/msg/Float32MultiArray`，且 publisher 来自当前 active `a2_policy_deploy`
  instance。monitor 自己不会成为 publisher。
- live output 显示 `sample_count`、latest age、dim 和 values first8。
- dim 6 时显示 `pred_base_lin_vel` 和 `pred_base_force_local`。
- dim 0、dim mismatch 或 NaN/Inf 都显示 `WARN`，但 monitor 本身不影响 active motion。
- 如果 terminal 暂时没有 sample，先确认 active policy node 已 history warm 并实际完成
  inference；stand-up/holder 或 history warmup 前可能还没有 aux message。
- 如果始终没有 sample，检查 `A2/config/a2_policy_remote.env` 或 operator env 中
  `A2_POLICY_PUBLISH_AUX_DEBUG=true`、`A2_POLICY_AUX_DEBUG_TOPIC=/a2/policy_aux`，并查看
  `policy_enable_remote_*.log` 或 `policy_listen_remote_*.log`。

## 10. Last Stage: enable_motion=true Remote Policy

这是 motion path。只在前面所有步骤 pass，并且 robot 离地/限功率、清场、e-stop 就位后运行。

再次确认：

- `motion-check` 显示内置 motion mode 已 release，或 App 已关闭内置 motion service。
- 现场没有其他 configured LowCmd topic publisher（默认 `/lowcmd`）。
- 遥控器 operator 知道 two-A sequence：first `A` 起身，holder 保持 policy default pose；second `A` 才开始 policy warmup/handover。
- second `A` 前让 `lx/rx/ly` stick 在 deadzone 后为 zero；否则 node 会继续 holder。
- `L2` 不再强制 locomotion command 为 zero；PolicyActive 中 valid sticks 会直接映射 command。
- Brake gate 是 real motion behavior：PolicyActive inference 后读取 aux
  `pred_base_force_local[0]`，默认 `<= -0.6` 连续 2 steps 且 forward/yaw eligibility
  满足时 latch。threshold 是 A2 observed unitless aux scale，不是 Newton。触发当前 tick
  不发布 zero LowCmd、不切 stop mode、不清 PD，也不跳过 policy joint command；该 tick
  继续正常 `publish_joint_commands()`，下一轮 observation command 才 override 为 zero，
  gait clock 同时 freeze 到 standing phase。
- Standing/walking gate 是 real policy observation behavior，但不是 stop path：它只控制
  gait clock freeze/advance，不改 raw requested command、不改 action、不改 LowCmd，也不绕过
  `publish_joint_commands()`。默认 hysteresis 是 `standing -> force_walking` when
  `force_xy >= 0.2`，`force_walking -> standing` when `force_xy <= 0.05`，其中
  `force_xy=sqrt(aux[3]^2 + aux[4]^2)`，不区分方向/符号。aux dim < 6 或 NaN/Inf 时不进入
  `force_walking`；当前若是 `force_walking` 则回 standing。brake active 优先级更高，
  会强制 command override zero + gait clock freeze standing，不允许 standing/walking gate 推进 gait phase。
- `Select` 是 primary local stop，会触发 local stop、runtime reset，并调用 `publish_zero()`
  发布 zero LowCmd。`L2+B` 只保留为附加 stop path；stand-up / hold / warmup 阶段 `B`
  rising edge 也会 cancel 并发布 zero LowCmd。该 zero stop 只属于 `enable_motion=true` 阶段。

按 run config 中的 remote speed caps 运行 remote policy，并在第二个 terminal 订阅 active aux topic：

```bash
A2_ALLOW_ENABLE_MOTION=1 A2/scripts/a2_real_robot_test.sh policy-enable-remote 20
```

另一个 Docker terminal：

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-monitor 0
```

脚本实际运行参数来自 `A2/config/a2_policy_remote.env`，并允许 operator shell 中已有的
`A2_POLICY_*` env 覆盖。默认 run config 值：

```text
enable_motion:=true
command_source:=remote
max_remote_vx:=0.80
max_remote_vy:=0.50
max_remote_yaw:=0.6
remote_deadzone:=0.08
require_standup_before_policy:=true
publish_aux_debug:=true
aux_debug_topic:=/a2/policy_aux
policy_aux_expected_dim:=6
policy_aux_print_period_sec:=0.2
standing_walking_gate_enabled:=true
standing_walking_enter_force_xy_threshold:=0.2
standing_walking_exit_force_xy_threshold:=0.05
brake_gate_enabled:=true
brake_force_x_threshold:=-0.6
brake_min_cmd_vx:=0.2
brake_max_abs_yaw:=0.10
brake_hold_steps:=2
```

Ideal result：

- policy contract validate 通过。
- lowstate fresh；first `A` 后进入 `StandUpInterpolating`，每 50 steps 打印 stage/front_alpha/rear_alpha，完成后进入 default pose holder。
- holder 阶段 second `A` 后进入 `PolicyWarmupHold`，持续发布 default stand pose，同时 history warm 到 `32` fresh frames。
- history warm 后 node 先做一次 policy action dim/finite validation；下一 valid cycle 才进入 `PolicyActive` 并发布 policy action。
- PolicyActive 中 centered sticks 对应 zero locomotion command；moving sticks 不要求
  `L2` held。
- 遥控方向应符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
- brake gate 只在 raw requested `cmd_vx >= 0.2`、`abs(cmd_yaw) <= 0.10`、非
  standing command 时 eligible；忽略 `vy`。触发后 latch，下一轮 policy observation
  command 为 `[0, 0, 0]`，gait clock freeze 到 standing phase，但 action validation 和
  `publish_joint_commands()` 继续执行。stick 回中/command standing、eligibility 不满足、
  local stop 或 runtime reset 会释放 latch。
- 第二个 terminal 的 `policy-aux-monitor` 用于观察 gate 前后的
  `pred_base_force_local[0]` 符号、阈值裕量和稳定性；monitor 不控制 gate。
- 无 stale state、NaN/Inf、action dim mismatch、CRC failure、robot abnormal behavior。

任何异常立即松开 sticks、按 primary local stop `Select` 或 e-stop，并保存 logs；`L2+B`
只作为附加 stop path。

## 11. Acceptance Checklist

连接 real A2 的首轮 validation 完成需要全部 pass：

- Host `enp131s0` 配置为 `192.168.123.99/24`，并能 ping `192.168.123.161`。
- container 使用 `A2_NET_IFACE=enp131s0`，`CYCLONEDDS_URI` 指向该 NIC。
- `connected-preflight` 对 configured `/lowstate` / `/lowcmd` visibility 和
  `unitree_hg` type check pass；`/lf/lowstate` 仅作 diagnostic info，不作为默认 backend。
- `no-lowcmd` observe-only check pass，确认进入任何 publish path 前 configured LowCmd topic
  没有 active messages。
- configured lowstate topic（默认 `/lowstate`）持续可见，rate/freshness/finiteness pass。
- `joints-live` observe-only 已逐关节验证 first-12 joint order 和 sign direction；旧 `joints` summary/CSV 可作为补充记录；如有 sign inversion 需求，已记录且未进入 motion path。
- `remote-live` observe-only 已验证 raw/display sticks 和 pressed buttons 可实时变化；旧 `remote` summary 和 `a2_lowlevel_smoke -p log_remote:=true` 输出一致。
- MotionSwitcher `CheckMode` 可读，`ReleaseMode` 或 App 能关闭 `ai_sports` / `ai_sport`。
- `zero-lowcmd` CRC、zero shape、`mode_machine` follow state 全部 pass。
- `policy-listen-remote` 在 `enable_motion=false` 下没有 configured LowCmd topic message。
- `policy-aux-live` 在 `enable_motion=false` 下没有 configured LowCmd topic message，并已确认
  aux dim/layout；brake gate 已实现，仍需在部署机/实机验证 `-0.6` 阈值、force x 符号和
  no zero-LowCmd stop、normal PD command continues、command override + gait-clock freeze /
  release 稳定性。
- `policy-aux-monitor` 可在 active `policy-enable-remote` 期间订阅 `/a2/policy_aux`，
  并实时显示 dim/value/warning；该 monitor 不启动 policy node，也不发布 LowCmd。
- `policy-enable-remote` 只在最后 stage、明确 guard、可控环境下运行，并验证 first `A`
  stand-up、holder default pose、second `A` warmup/handover、local stop 和 `B` cancel。
- 测试结束恢复内置 motion service 前，已停止 policy/LowCmd publisher、`no-lowcmd` 重新 pass，
  并通过 guarded `motion-restore` / `motion-select` 或 Unitree App 恢复。

## 12. Failure Logs to Collect

所有脚本默认写 log 到：

```bash
ls -l /tmp/a2_real_robot_tests
```

失败时回传：

```bash
cd /work/projects/AliengoSim2Real/ros2
git rev-parse --short HEAD
env | grep -E '^(ROS_DISTRO|RMW_IMPLEMENTATION|A2_NET_IFACE|A2_LOWSTATE_TOPIC|A2_LOWCMD_TOPIC|CYCLONEDDS_URI|Torch_DIR)='
ip addr show enp131s0
ping -c 5 192.168.123.161
ros2 topic list
ros2 topic info /lowstate -v || true
ros2 topic info /lf/lowstate -v || true
ros2 topic info /lowcmd -v || true
find /tmp/a2_real_robot_tests -maxdepth 1 -type f -name '*.log' -print
```

需要重点回传：

- `connected_preflight_*.log`
- `no_lowcmd_*.log`
- `lowstate_*.log`
- `joints_live_*.log`
- `joints_*.log`
- `remote_live_*.log`
- `remote_*.log`
- `smoke_remote_*.log`
- `motion_check_*.log`
- `motion_release_*.log`
- `motion_select_*.log`
- `zero_lowcmd_observer_*.log`
- `zero_lowcmd_smoke_*.log`
- `policy_listen_remote_*.log`
- `policy_listen_no_lowcmd_*.log`
- `policy_aux_live_*.log`
- `policy_aux_live_no_lowcmd_*.log`
- `policy_aux_monitor_*.log`
- `policy_enable_remote_*.log`

## 13. Official References

- `ros2/A2_Guide/html/11-develop_module.html`
  - A2 PC1 Ethernet one 是 `192.168.123.0/24`；SDK development 只能在 123 subnet；可 ping `192.168.123.161`；124 subnet 不是 SDK dev。
- `ros2/A2_Guide/html/10-quick_development.html`
  - 官方 quick development 用 `ping 192.168.123.161` 检查连接，并用 network interface 运行 examples。
- `ros2/A2_Guide/html/12-dds_service.html`
  - Unitree SDK2 wraps DDS；`ChannelFactory::Init(0, networkInterface, false)` 语义中 external dev 需要 `enableSharedMemory=false`；common topics 包括 `rt/lowstate` 和 `rt/lowcmd`；messages 来自 Unitree ROS2 msg。
- `ros2/A2_Guide/html/13-basic_service_interface.html`
  - A2 low-level service 使用 DDS；订阅 `rt/lowstate` type `unitree_hg::msg::dds_::LowState_`；发布 `rt/lowcmd` type `unitree_hg::msg::dds_::LowCmd_`；low-level control 前必须通过 MotionSwitcherClient 或 App 关闭 `ai_sports`；`mode_machine` 必须和 lowstate 对齐；`MotorCmd.mode` STOP `0x00`、FOC `0x01`；`wireless_remote[40]`；`tick` 是 1ms counter；`crc` 是 CRC32。
- `ros2/A2_Guide/html/17-motion_witcher_service_interface.html`
  - `MotionSwitcherClient` supports `CheckMode`、`SelectMode`、`ReleaseMode`；`ReleaseMode` 用于 release motion mode，`SelectMode` 用于恢复指定 motion mode。
- `ros2/A2_Guide/html/05-a2_remote_control.html`
  - A2 R3 remote button concepts，包括 `L2+B` damping、Start resume 和 binding notes。
- `/Users/caobaoquan/Downloads/python/projects/third_party/unitree/unitree_sdk2_python/example/wireless_controller/wireless_controller.py`
  - official remote decode sample：byte2 bits `R1,L1,Start,Select,R2,L2,F1,F3`；byte3 bits `A,B,X,Y,Up,Right,Down,Left`；float offsets `lx=4`、`rx=8`、`ry=12`、`ly=20`，little-endian。
- `/Users/caobaoquan/Downloads/python/projects/third_party/unitree/unitree_ros2/example/src/include/common/motor_crc_hg.h`
  - official raw `LowCmd` / `MotorCmd` layout。
- `/Users/caobaoquan/Downloads/python/projects/third_party/unitree/unitree_ros2/example/src/src/common/motor_crc_hg.cpp`
  - official CRC32 algorithm and CRC over raw `LowCmd` words before `crc`。
- `/Users/caobaoquan/Downloads/python/projects/third_party/unitree/unitree_sdk2/include/unitree/dds_wrapper/robots/go2/go2.h`
  - official wrapper calls `MotionSwitcherClient::ReleaseMode()` before publishing `rt/lowcmd`。
