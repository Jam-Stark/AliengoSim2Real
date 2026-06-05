---
name: a2_deploy_progress
scope: ros2/A2
status: active
last_updated: "2026-06-05 22:54 HKT"
owned_paths:
  - ros2/A2/
  - ros2/A2_Guide/
read_when:
  - 修改 A2 ROS2 low-level deployment、A2 policy deploy、unitree_hg interface、ROS2 visible /lowstate / /lowcmd routing、official DDS rt/lowstate / rt/lowcmd routing 或 deploy machine readiness 时
---

## Purpose

本 entry 记录 `ros2/A2` 的独立 A2 ROS2 low-level deployment 起点。当前 A2 work 不修改既有 Go2W `ros2/src/**` 链路。

已完成事实：

- 新建独立 ament package `a2_lowlevel`。
- 实现标准 A2 12-motor low-level adapter：默认订阅 ROS2 visible `/lowstate`，默认发布 `/lowcmd`，使用 `unitree_hg` ROS2 messages；official A2_Guide DDS reference names 仍是 `rt/lowstate` / `rt/lowcmd`。
- `A2LowLevelInterface` 支持 `lowstate_topic` / `lowcmd_topic` parameters，默认 `lowstate` / `lowcmd`，可按现场 ROS2 graph 切换；不要用 ROS remap 代替该 backend 参数。
- 实现 `A2LowLevelInterface` public API：`latest_state()`、`has_fresh_state()`、`publish_zero()`、`publish_joint_commands()`。
- 实现 A2 专属 CRC，未复用 Go2W `motor_crc`。
- 实现 `a2_lowlevel_smoke`，默认 listen-only，`publish_zero` / `stand_test` 需要显式参数。
- 实现可选 `a2_policy_deploy` target：`BUILD_A2_POLICY_DEPLOY=ON` 时才查找 LibTorch/jsoncpp，并通过 shared `ManagerBasedEnv` / `Policy` runtime 加载 `policy/A2_policy/policy.pt`。
- `a2_policy_deploy` 校验 `policy/A2_policy/policy.json` contract：`action_dim=12`、`per_frame_obs_dim=46`、`history_length=32`、`action_scale=0.25`、`sim_dt=0.005`、`control_decimation=4`、训练 joint order。
- A2 policy observation 每帧为 `projected_gravity_xy(2)`、`base_ang_vel(3)*0.25`、`joint_q-default_pos(12)`、`joint_dq*0.05`、`last_raw_action(12)`、`gait_clock(2)`、`command(3)*[2,2,0.25]`，history `32` flatten 为 `1472`。
- A2 policy action 先按 `action_clip` clip，再映射为 `default_joint_pos + action_scale * raw_action`；训练顺序到 A2 low-level order same signs / no inversion，低层发布只走 `A2LowLevelInterface::publish_joint_commands()`。
- 实现 A2 Remote Control v1 decode contract：`wireless_remote[40]` 中 `lx/rx/ry/ly` 分别来自 offsets `4/8/12/20` 的 little-endian `float32`，button byte `2/3` 按 Unitree SDK2 sample bit order decode，并做 `deadzone`、`[-1,1]` clamp 和 NaN/Inf invalid guard。
- `a2_policy_deploy` 支持 `command_source=static|remote`，默认 `static`；remote mapping 为 `cmd_vx=ly*max_remote_vx`、`cmd_vy=-lx*max_remote_vy`、`cmd_yaw=-rx*max_remote_yaw`，`L2` 是 nonzero command gate；`Select` 或 `L2+B` 触发 local stop、`set_zero_command()` 和 policy runtime reset，只有 `enable_motion=true` 时才额外 `publish_zero()`。
- `a2_policy_deploy` 已实现 A2 Stand-Up + Policy Handover gate：默认 `require_standup_before_policy=true`，`enable_motion=true` / `command_source=remote` 下 first `A` 触发 stand-up interpolation，holder 持续发布 policy `default_joint_pos`，second `A` 在 `L2` released 且 `lx/rx/ly` deadzone 后为 zero 时进入 `PolicyWarmupHold`，history warm 和 first action validation 后下一 cycle 进入 `PolicyActive`。
- stand-up / holder / warmup command 仍只调用 `A2LowLevelInterface::publish_joint_commands()`，不直接写 `unitree_hg::msg::LowCmd`，不绕过 fresh-state、mode routing 或 CRC；`command_source=static` 在默认 stand-up gate 下会拒绝 `enable_motion=true` motion publish，除非显式设置 `require_standup_before_policy=false`。
- remote safety gate 已扩展：`Select` / `L2+B` 任意 phase local stop，stand-up / holder / warmup 阶段 `B` rising edge cancel；`enable_motion=false` 下不发布 zero LowCmd，`enable_motion=true` 下才发布 zero LowCmd。
- `a2_lowlevel_smoke` 支持 `log_remote` listen-only decode logging，打印 sticks 和 button names。
- 实现部署机信息采集脚本 `ros2/A2/scripts/collect_deploy_machine_info.sh`，用于生成 `DeployMachineINFO.md`。
- 当前 code machine 的 Unitree reference repos 已移动到 `/Users/caobaoquan/Downloads/python/projects/third_party/unitree`，即 `AliengoSim2Real` 同级 parent `projects` 下的 `third_party/unitree`；部署机也计划使用同样的 parent-projects layout。
- 已收到部署机报告 `ros2/A2/DeployMachineINFO.md`：host 是 `lt5.precognition.team` / user `baoquanc`，workspace 是 `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`，host OS 是 Ubuntu 24.04.3，host 未安装 `/opt/ros`，candidate A2 NIC 是 `enp131s0`，当前未配置 `192.168.123.x`。
- 部署机 Unitree refs 已确认：`unitree_ros2@5204e6e`、`unitree_sdk2@63c6f53`、`unitree_sdk2_python@f7a5526`，路径是 `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree`。
- 已实现 `ros2/A2/docker/` Docker deployment layer：基于 Ubuntu 22.04 + ROS2 Humble container，安装 apt CycloneDDS/RMW、Unitree ROS2 messages、SDK2 examples、CPU LibTorch `/opt/libtorch`，并提供 host image build/run/preflight 和 container workspace build scripts。
- A2 Docker formal target platform 是 `linux/amd64`，因为正式 deploy machine 是 x86_64；`build_image.sh` / `run_container.sh` 默认使用 `A2_DOCKER_PLATFORM=linux/amd64`，仅 debug 特殊架构时覆盖。
- A2 Docker 默认 `A2_NET_IFACE=lo` 用于 offline smoke；真实 A2 需要显式 `A2_NET_IFACE=enp131s0`，并手动把 host NIC 配到 `192.168.123.99/24`。`192.168.124.x` 不是当前 SDK2 low-level DDS chain 使用的 subnet。
- Apple Silicon Mac Docker Desktop 可在 amd64 emulation 下运行大部分 offline validation：image build、container startup、lowlevel/policy build、ROS2 interface checks、listen-only smoke；但性能、timing、Docker Desktop host networking 和 DDS/network/control 不具备部署代表性。
- 本 thread 已决定跳过 Mac Docker Desktop offline validation；未在 Mac 上 run Docker build/container，不把该 validation 记为 DONE，下一步直接走 deploy-machine Docker build/preflight。
- 已新增部署机专用 Docker build/test 文档 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md`，覆盖 image build、preflight、offline container environment、`unitree_hg` interface checks、lowlevel/policy build、fake lowstate smoke、`enable_motion=false` no-lowcmd verification、optional zero-command path 和实机前 acceptance checklist。
- 用户已报告 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md` 中无连接 robot 的 Docker virtual tests 全部通过：Docker image、connected-independent preflight/container readiness、`unitree_hg` interface checks、lowlevel/policy build、offline lowstate/fake-lowstate smoke、`enable_motion=false` no-lowcmd verification。
- 已新增 real robot validation guide/scripts：`ros2/A2/scripts/A2_REAL_ROBOT_TEST.md`、`a2_real_robot_test.sh`、`a2_real_robot_observer.py`，覆盖 connected preflight、真实 configured lowstate（默认 `/lowstate`）rate/tick/freshness、remote raw/decode、MotionSwitcher `CheckMode` / guarded `ReleaseMode`、guarded zero `LowCmd` CRC、policy listen-only no-lowcmd 和 guarded `enable_motion=true` remote policy。
- 已在 real robot validation guide/scripts 中新增 joint state mapping/direction observe-only validation：`a2_real_robot_observer.py joints` 订阅 configured lowstate（默认 `/lowstate`）、读取 first-12 `motor_state`、固定 A2 joint labels、统计 q/dq/range/max_abs_dq、支持 CSV；`a2_real_robot_test.sh joints` 暴露 `A2_JOINT_PRINT_PERIOD`、`A2_JOINT_MIN_DELTA`、`A2_JOINT_CSV`。
- 已新增 A2 real robot live observation tools：`a2_real_robot_observer.py joints-live` / `remote-live` 和 wrapper `a2_real_robot_test.sh joints-live` / `remote-live`，均只订阅 configured lowstate topic（默认 `/lowstate`）、不发布 `LowCmd`；`joints-live` 周期刷新 12 joint q/dq/delta/range live table，`remote-live` 周期刷新 raw/display sticks、pressed buttons 和 valid flag。
- 用户在部署机 + real A2 connected-preflight 中确认网络和 DDS 正常：`enp131s0` UP、IP `192.168.123.222/24`、ping `192.168.123.161` 成功；ROS2 graph 没有 `/rt/lowstate`，而是可见 `/lowstate`、`/lowcmd`、`/lowstate_raw`、`/lf/lowstate`、`/wirelesscontroller` 等。因此 A2 ROS2 backend default topic 已修正为 `/lowstate` / `/lowcmd`。
- 用户提供 `ros2/A2/scripts/connected_preflight_result.md`：configured `/lowstate` 可见且 type 为 `unitree_hg/msg/LowState`，configured `/lowcmd` 可见且 type 为 `unitree_hg/msg/LowCmd`；`/lowcmd` 在 preflight 阶段已有 bare DDS Publisher / Subscription endpoint，但这不等价于 active command messages。
- 同一 preflight result 显示 `/lf/lowstate` type 为 `['unitree_go/msg/LowState', 'unitree_hg/msg/LowState']`，存在 type ambiguity；它只作为 diagnostic info，不作为默认 A2 policy / lowlevel backend topic，默认继续使用 `/lowstate`。
- real robot validation script/docs 已 harden：`connected-preflight` 额外校验 configured lowstate / lowcmd topic type；新增 standalone `no-lowcmd` observe-only step，进入任何 configured LowCmd publish path 前必须确认无 active LowCmd traffic。
- `a2_real_robot_test.sh` MotionSwitcher helper compile/runtime 已适配部署机 SDK2 DDS nested include/lib layout：自动加入 `install/include/ddscxx`、`thirdparty/include/ddscxx`、`install/lib`、`thirdparty/lib/$(uname -m)` 等候选，显式链接 `-lddscxx -lddsc`，并通过 wrapper 将 SDK2 lib dirs 放到 `LD_LIBRARY_PATH` 前面以避免 ROS2/CycloneDDS libs shadow；`motion-check` 仍只调用 `CheckMode`，`motion-release` 仍需 `A2_ALLOW_RELEASE_MODE=1`。

当前 blocker：

- code machine 没有 ROS2 / `colcon` / `/opt/ros`，无法本地完整 build `a2_lowlevel`。
- code machine 是 macOS，但本 thread 明确不跑 Docker Desktop offline validation；真实 DDS/network/control 仍需在 Linux deploy machine + A2 网络上验证。
- 部署机 host 是 Ubuntu 24.04.3，因此 A2 deploy 采用 Ubuntu 22.04 + ROS2 Humble Docker container；不要要求 host 原生安装 Humble。
- A2 CRC 仍需和部署机 `unitree_hg` generated messages、Unitree SDK2 sample 或实机 low-level command 行为对照验证。
- A2 R3 remote layout 已按 Unitree SDK2 sample 实现，但仍需在部署机/实机用真实 configured lowstate（默认 `/lowstate`）的 `wireless_remote[40]` 验证 stick/button 方向、`L2` gate 和 local stop button。
- 低层实机控制前必须确认 Unitree 内置运动服务 `ai_sport` / `ai_sports` 已关闭；A2 runtime node 不自动调用 `MotionSwitcherClient`，real robot validation script 已提供 guarded `motion-check` / `motion-release` helper，但仍需部署机实机验证。
- 进入任何 real configured LowCmd topic publish path 前，必须先运行 `A2/scripts/a2_real_robot_test.sh no-lowcmd 5`；如果观察到 LowCmd message，说明现场已有 active LowCmd traffic，必须停止该 publisher 后再继续。

## When Codex/AI Should Read This Entry

- 修改 A2 `a2_lowlevel` package、low-level adapter、CRC、smoke node 或 deploy machine info collector。
- 修改 A2 policy deploy、policy contract、observation/action mapping、ManagerBasedEnv adapter boundary。
- 需要判断 A2 low-level deployment 当前 blocker、部署机验证步骤或安全前置条件。

## Source Paths

- `ros2/A2/package.xml`
- `ros2/A2/CMakeLists.txt`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_policy_deploy_node.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/include/a2_lowlevel/a2_remote.h`
- `ros2/A2/src/a2_lowlevel_interface.cpp`
- `ros2/A2/src/a2_policy_deploy_node.cpp`
- `ros2/A2/src/a2_crc.cpp`
- `ros2/A2/src/a2_remote.cpp`
- `ros2/A2/src/a2_lowlevel_smoke.cpp`
- `ros2/A2/test/a2_remote_decode_test.cpp`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md`
- `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md`
- `ros2/A2/scripts/a2_real_robot_test.sh`
- `ros2/A2/scripts/a2_real_robot_observer.py`
- `ros2/A2/docker/Dockerfile`
- `ros2/A2/docker/entrypoint.sh`
- `ros2/A2/docker/build_image.sh`
- `ros2/A2/docker/run_container.sh`
- `ros2/A2/docker/build_a2_workspace.sh`
- `ros2/A2/docker/preflight.sh`
- `ros2/A2/DeployMachineINFO.md`
- `ros2/A2/README.md`
- `policy/A2_policy/policy.pt`
- `policy/A2_policy/policy.json`
- `ros2/A2_Guide/`

## TODO Summary

- 部署机已观测到 `enp131s0` 使用 `192.168.123.222/24` 且可 ping A2 `192.168.123.161`；如 host network reset，重新恢复 `192.168.123.x` low-level subnet，不要把 `192.168.124.x` 当作 SDK2 low-level subnet。
- 用更新后的 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md` 继续 connected real A2 tests，并回传 `/tmp/a2_real_robot_tests` logs：新版 `connected-preflight enp131s0` PASS、`no-lowcmd 5` observe-only、configured `/lowstate` lowstate rate/tick/freshness、`joints-live` order/sign observe-only、`remote-live` raw/decode live observe、MotionSwitcher `motion-check` helper compile / `ldd` / stage log / `CheckMode` 和 guarded release、zero `LowCmd` CRC、policy listen-only 和 guarded `enable_motion=true`；旧 `joints` / `remote` 可作为 summary/CSV validation。
- 在部署机/实机按 `A2_REAL_ROBOT_TEST.md` 先用 `joints-live` 逐关节验证 joint order/direction，并记录是否需要 per-joint sign inversion；未完成前不要进入 control path。
- 用实机 zero `LowCmd` 和官方 raw layout/CRC 对照 A2 CRC；如不一致，修正 `a2_crc` raw layout。
- 首次实机前确认 `ai_sport` / `ai_sports` 关闭、离地或限功率 smoke、hardware emergency stop。
- 在部署机/实机先用 `remote-live` 验证 A2 R3 remote raw/display sticks 和 pressed buttons，再用旧 `remote` / `a2_lowlevel_smoke log_remote` 做 summary/smoke 对照；随后验证 `a2_policy_deploy command_source=remote` 的 `L2` gate、`Select` / `L2+B` local stop `enable_motion` 分流和 mapping 方向。
- 在部署机/实机验证 guarded `policy-enable-remote` 的 two-A handover：first `A` stand-up interpolation、default pose holder、second `A` warmup/handover、下一 cycle `PolicyActive`、`Select` / `L2+B` local stop 和 stand-up / holder / warmup 阶段 `B` cancel。

## DONE Summary

- A2 low-level adapter、smoke node、deploy machine info collector、README 首版已完成。
- A2 memory 已规范化为 root memory schema，并只引用 `ros2/A2_Guide/`，不复制长 A2 SDK docs。
- Unitree reference repos 路径已从旧 home-level default path 更新为 parent-projects layout：`/Users/caobaoquan/Downloads/python/projects/third_party/unitree`。
- A2 Policy Adapter v1 已实现：guarded CMake target、`A2PolicyDeployNode`、policy contract validation、observation/action mapping、安全 publish gating、README policy sections。
- A2 Remote Control v1 已实现：remote decode utility、policy `command_source=remote`、`L2` button gate、local stop、smoke remote logging、README remote sections 和 compile-free fake-byte decode test。
- A2 Docker deployment layer 已实现：Ubuntu 22.04 + ROS2 Humble image、Unitree refs pin、CPU LibTorch default、host run/preflight scripts、container build script、README Docker build/run/smoke/migration sections。
- A2 Docker helper script / README / memory 已记录 formal `linux/amd64` default 和 Mac Docker Desktop offline validation scope。
- 本 thread 已记录跳过 Mac Docker Desktop offline validation，且 deploy-machine Docker build/preflight 是下一条 validation path。
- A2 Docker build/test deploy-machine guide 已新增到 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md`；文档步骤已明确不连接 real A2、不启用 motion、不用 Mac Docker networking 代表 DDS/control。
- 用户已报告部署机无连接 robot 的 Docker virtual tests 全部通过：image/container、`unitree_hg` interface、lowlevel/policy build、offline smoke 和 `enable_motion=false` no-lowcmd verification。
- A2 real robot validation guide/scripts 已新增：`A2_REAL_ROBOT_TEST.md`、`a2_real_robot_test.sh`、`a2_real_robot_observer.py`，默认 observe-only，并用 env guard 保护 `ReleaseMode`、zero `LowCmd` 和 `enable_motion=true`。
- A2 joint state mapping/direction observe-only validation 已新增：`joints` observer/wrapper/docs 订阅 configured lowstate（默认 `/lowstate`），不发布 LowCmd，用于实机控制前确认 first-12 joint order 和 sign direction。
- 2026-06-05 21:21 HKT 已记录真机 connected-preflight topic mismatch：ROS2 graph 可见 `/lowstate` / `/lowcmd` 而不是 `/rt/lowstate`；A2 ROS2 backend、observer、wrapper 和文档默认 topic 已改为 `/lowstate` / `/lowcmd`，保留参数/env override。
- 2026-06-05 21:43 HKT 已根据 `connected_preflight_result.md` harden real robot preflight：记录 configured `/lowstate` / `/lowcmd` visibility/type pass、`/lf/lowstate` type ambiguity，并新增 `no-lowcmd` observe-only traffic check 作为任何 publish path 前的安全检查。
- 2026-06-05 21:52 HKT 已修复 A2 policy listen-only safety P1：remote `Select` / `L2+B` local stop 在 `enable_motion=false` 下只 reset runtime、不发布 zero LowCmd；`enable_motion=true` 下保持 zero LowCmd stop。
- 2026-06-05 22:03 HKT 已新增 `joints-live` / `remote-live` observe-only tools、wrapper env 和 docs，用于实时人工确认 joint mapping 及 remote raw/decode；当前 TODO 已调整为先用 live tools 验证。
- 2026-06-05 22:20 HKT 已修复 MotionSwitcher helper 手写 `g++` compile path：自动去重打印 SDK2 include/lib dirs，加入 nested DDS `ddscxx` / `ddsc` headers 和 DDS lib dirs，并链接 `ddscxx` / `ddsc`，避免 `dds/topic/TopicTraits.hpp` header 修复后继续出现 DDS unresolved symbols。
- 2026-06-05 22:32 HKT 已针对部署机 `motion-check` helper runtime `free(): invalid pointer` 加固：helper 通过 wrapper 前置 SDK2 `LD_LIBRARY_PATH`，打印 `ldd` 结果和 `ChannelFactory::Init` / `MotionSwitcherClient::Init` / `CheckMode` / `ChannelFactory::Release` 阶段日志，并在成功路径显式 release SDK2 channel factory。
- 2026-06-05 22:54 HKT 已实现 A2 Stand-Up + Policy Handover gate：默认 remote two-A handover，stand-up/holder/warmup command 只走 `publish_joint_commands()`，static motion 默认 blocked unless `require_standup_before_policy=false`，并更新 real robot script/docs/README/memory。

## Recommended Next Files To Read

- `ros2/A2/README.md`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/memory/a2_deploy_progress/TODO.md`
- `ros2/A2/memory/a2_deploy_progress/DONE.md`
- `memory/shared_policy_runtime/description.md`
