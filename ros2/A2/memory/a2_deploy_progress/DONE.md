# DONE

## 2026-06-04 14:34 HKT

- [x] 确认项目根目录没有顶层 `MEMORY.md`，因此原项目没有可用 root memory 入口。
- [x] 新建 `ros2/A2` 独立 A2 deployment workspace area。
- [x] 新建 `a2_lowlevel` ament package，依赖 `ament_cmake`、`rclcpp`、`unitree_hg`。
- [x] 实现标准 A2 12-motor order：
  - `FR_BODY`
  - `FR_THIGH`
  - `FR_CALF`
  - `FL_BODY`
  - `FL_THIGH`
  - `FL_CALF`
  - `RR_BODY`
  - `RR_THIGH`
  - `RR_CALF`
  - `RL_BODY`
  - `RL_THIGH`
  - `RL_CALF`
- [x] 实现 `A2LowLevelInterface`：
  - subscribe `rt/lowstate`
  - publish `rt/lowcmd`
  - latest state snapshot
  - fresh-state guard
  - safe zero command
  - 12-joint command publishing
- [x] 实现 A2 独立 CRC，不复用 Go2W `motor_crc.cpp`。
- [x] 实现 `a2_lowlevel_smoke`：
  - 默认 listen-only
  - `publish_zero`
  - `stand_test`
  - `state_timeout_ms`
  - `command_hz`
- [x] 编写 `ros2/A2/README.md`，记录 build/run、topic/type、12 motor order、安全提醒和 policy boundary。
- [x] 在 code machine 的 `/Users/caobaoquan/Downloads/python/projects/third_party/unitree` clone：
  - `unitree_ros2`
  - `unitree_sdk2`
  - `unitree_sdk2_python`
- [x] 只读确认 `unitree_ros2` 中存在 `unitree_hg/msg/LowCmd.msg`、`LowState.msg`、`MotorCmd.msg`，字段名与当前 A2 adapter 假设一致。
- [x] 实现 `ros2/A2/scripts/collect_deploy_machine_info.sh`，用于部署机生成 `DeployMachineINFO.md`。
- [x] 本地验证：
  - `xmllint --noout ros2/A2/package.xml` 通过。
  - `bash -n ros2/A2/scripts/collect_deploy_machine_info.sh` 通过。
  - `bash ros2/A2/scripts/collect_deploy_machine_info.sh --help` 通过。
  - `bash ros2/A2/scripts/collect_deploy_machine_info.sh > /tmp/a2_deploy_machine_info_smoke.md` 在 macOS / non-ROS 环境生成 Markdown report。

## 2026-06-04 14:48 HKT

- [x] 将 A2 memory 规范化为 root memory schema：`description.md` 增加 required YAML frontmatter 和 required sections。
- [x] 保留 A2 low-level adapter、smoke node、deploy machine info collector、部署机 blocker 和 TODO/DONE summary 事实。
- [x] A2 SDK/reference docs 只引用 `ros2/A2_Guide/`，未复制长文档到 memory。

## 2026-06-04 19:31 HKT

- [x] 将 Unitree reference repos memory 路径从旧 home-level default path 更新为 parent-projects layout：`/Users/caobaoquan/Downloads/python/projects/third_party/unitree`。
- [x] 更新部署机 TODO 命令，使用 `UNITREE_ROOT="$(cd .. && pwd)/third_party/unitree"` 并显式传给 `collect_deploy_machine_info.sh --unitree-root`。

## 2026-06-05 15:07 HKT

- [x] 实现 A2 Policy Adapter v1 optional CMake target：默认不查找 LibTorch/jsoncpp，`-DBUILD_A2_POLICY_DEPLOY=ON` 时才 build `a2_policy_deploy`。
- [x] 新增 `A2PolicyDeployNode`，通过 shared `ManagerBasedEnv` / `Policy` runtime 加载 `policy/A2_policy/policy.pt`，并读取 `policy/A2_policy/policy.json` 做 contract validation。
- [x] 定义 A2 policy observation contract：每帧 `46` dims、history `32`、flatten `1472`，包含 projected gravity、base angular velocity、joint q/dq、last raw action、gait clock、static command。
- [x] 定义 action contract 和 mapping：training joint order 到 A2 low-level order same signs / no inversion，raw action clip 后按 `default_joint_pos + 0.25 * raw_action` 生成 target q，hip/thigh/calf 使用固定 PD gains。
- [x] 保持 low-level publish boundary：policy node 只调用 `A2LowLevelInterface::publish_joint_commands()`，不直接写 `unitree_hg::msg::LowCmd`，不绕过 fresh-state guard、mode routing 或 A2 CRC。
- [x] 增加 policy safety gating：missing/stale state、history 未 warm、`enable_motion=false`、NaN/Inf、wrong observation/action dim 时拒绝发布 motion。
- [x] 更新 `ros2/A2/README.md` 的 policy build/run、contract、mapping、safety 和 remote TODO。

## 2026-06-05 16:52 HKT

- [x] 实现 A2 remote decode utility：从 `wireless_remote[40]` 按 Unitree SDK2 sample offsets `lx=4`、`rx=8`、`ry=12`、`ly=20` decode little-endian `float32`，并按 byte `2/3` bit layout decode buttons。
- [x] remote decode 增加 `deadzone`、`[-1,1]` clamp 和 NaN/Inf invalid guard；invalid stick data 不进入 policy command。
- [x] `a2_lowlevel_smoke` 增加 `log_remote` listen-only logging，打印 sticks 和 button names，默认仍不发布 command。
- [x] `a2_policy_deploy` 增加 `command_source=static|remote`，保留 static command params，并实现 remote mapping、remote local stop、runtime reset 和 `enable_motion=true` zero stop。
- [x] 更新 `ros2/A2/README.md` remote build/run、joystick mapping、safety limits 和 deploy machine validation checklist。

## 2026-06-05 18:14 HKT

- [x] 收到并纳入 `ros2/A2/DeployMachineINFO.md`：部署机 `lt5.precognition.team` / user `baoquanc`，workspace `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`，host OS Ubuntu 24.04.3，host 未安装 `/opt/ros`，candidate A2 NIC `enp131s0` 当前未配置 `192.168.123.x`。
- [x] 确认部署机 Unitree refs：`unitree_ros2@5204e6e`、`unitree_sdk2@63c6f53`、`unitree_sdk2_python@f7a5526`，并将 Dockerfile pin 到这些 refs。
- [x] 实现 `ros2/A2/docker/` Docker deployment layer：Ubuntu 22.04 + ROS2 Humble image、apt CycloneDDS/RMW、Unitree ROS2 messages、SDK2 examples、CPU LibTorch `/opt/libtorch`、entrypoint、host image build/run/preflight scripts、container workspace build script。
- [x] 更新 `ros2/A2/README.md`：Docker build/run、preflight、offline smoke、connected smoke、remote logging、policy remote run、Docker migration/export/import 和 safety gates。

## 2026-06-05 18:31 HKT

- [x] 将 A2 Docker helper 默认 platform 固定为 `A2_DOCKER_PLATFORM=linux/amd64`，匹配正式 x86_64 deploy machine；`build_image.sh` / `run_container.sh` 默认传入 Docker `--platform`，`preflight.sh` 只报告/使用 platform 做 container check，不修改 host state。
- [x] 更新 `ros2/A2/README.md` 和 A2 memory，说明 Apple Silicon Mac Docker Desktop 可用 amd64 emulation 做 offline validation，但 timing、host networking、DDS 和实机 control 仍属于部署机验证范围。

## 2026-06-05 18:34 HKT

- [x] 按本 thread 决策移除 Mac Docker Desktop offline validation active TODO；该 validation 未运行、未标记完成，下一步 validation path 改为 deploy-machine Docker build 和 `preflight.sh --iface enp131s0 --container-check`。

## 2026-06-05 18:41 HKT

- [x] 新增 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md`，面向部署机 `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real` 记录 Docker image build、preflight、offline `A2_NET_IFACE=lo` container checks、`unitree_hg` interface checks、lowlevel/policy build、fake lowstate smoke、`enable_motion=false` no-lowcmd verification、optional zero-command path、实机前 acceptance checklist 和 failure log 收集。

## 2026-06-05 19:49 HKT

- [x] 用户报告 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md` 中无连接 robot 的 Docker virtual tests 全部通过，覆盖 Docker image/container readiness、`unitree_hg` interface checks、lowlevel/policy build、offline smoke 和 `enable_motion=false` no-lowcmd verification。
- [x] 新增 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md`，记录 real A2 connected validation 顺序：host `enp131s0` / `192.168.123.99/24` / ping、container connected preflight、真实 `/rt/lowstate`、remote raw/decode、MotionSwitcher `CheckMode` / guarded `ReleaseMode`、guarded zero `LowCmd` CRC、policy listen-only 和 last-stage guarded `enable_motion=true`。
- [x] 新增 `ros2/A2/scripts/a2_real_robot_test.sh` orchestrator，提供 `connected-preflight`、`lowstate`、`remote`、`smoke-remote`、`motion-check`、guarded `motion-release`、guarded `zero-lowcmd`、`policy-listen-remote`、guarded `policy-enable-remote` subcommands，并写 logs 到 `/tmp/a2_real_robot_tests`。
- [x] 新增 `ros2/A2/scripts/a2_real_robot_observer.py` ROS2 observer，独立验证 `/rt/lowstate` rate/tick/freshness、official remote byte decode、`LowCmd` raw-layout CRC/zero/mode checks 和 no-lowcmd 监听。

## 2026-06-05 20:10 HKT

- [x] 已添加 A2 joint state mapping/direction observe-only validation scripts/docs：

  - `a2_real_robot_observer.py joints` 只订阅 `/rt/lowstate`，不发布 `/rt/lowcmd`。
  - 固定 first-12 labels：`FR_BODY`、`FR_THIGH`、`FR_CALF`、`FL_BODY`、`FL_THIGH`、`FL_CALF`、`RR_BODY`、`RR_THIGH`、`RR_CALF`、`RL_BODY`、`RL_THIGH`、`RL_CALF`。
  - 记录 q start/end/min/max/range、max abs dq、周期性 q/dq/delta 快照、`candidate_changed_joints` 和 optional CSV time series。
  - `a2_real_robot_test.sh joints` 支持 `A2_JOINT_PRINT_PERIOD`、`A2_JOINT_MIN_DELTA`、`A2_JOINT_CSV`。
  - `A2_REAL_ROBOT_TEST.md` 已将逐关节 order/sign observe-only 验证放在 continuous lowstate 后、remote 前。

## 2026-06-05 21:21 HKT

- [x] 记录部署机 + real A2 connected-preflight 的 ROS2 topic namespace mismatch：网络和 DDS 正常，`enp131s0` UP、IP `192.168.123.222/24`、ping `192.168.123.161` 成功；ROS2 graph 可见 `/lowstate`、`/lowcmd`、`/lowstate_raw`、`/lf/lowstate`、`/wirelesscontroller`，没有 `/rt/lowstate`。
- [x] 将 A2 ROS2 backend default topic 修正为 ROS2 visible `/lowstate` / `/lowcmd`：`A2LowLevelInterface` 参数 `lowstate_topic` / `lowcmd_topic` 默认 `lowstate` / `lowcmd`，并在 smoke/policy log 中使用实际 resolved topic。
- [x] 更新 `a2_real_robot_observer.py`、`a2_real_robot_test.sh`、`A2_REAL_ROBOT_TEST.md`、`README.md`、`A2_DOCKER_BUILD_TEST.md`，默认走 `/lowstate` / `/lowcmd`，同时保留 official DDS `rt/lowstate` / `rt/lowcmd` 作为 reference name 和 override 说明。

## 2026-06-05 21:43 HKT

- [x] 纳入 `ros2/A2/scripts/connected_preflight_result.md` 结论：configured `/lowstate` 可见且 type 包含 `unitree_hg/msg/LowState`，configured `/lowcmd` 可见且 type 包含 `unitree_hg/msg/LowCmd`，网络 `enp131s0` / `192.168.123.222/24` / ping `192.168.123.161` 正常。
- [x] 记录 `/lowcmd` preflight 阶段 bare DDS Publisher / Subscription endpoint 不等价于 active command traffic；进入任何 publish path 前新增 standalone `no-lowcmd` observe-only check。
- [x] 记录 `/lf/lowstate` 当前 type ambiguity：`unitree_go/msg/LowState` 与 `unitree_hg/msg/LowState` 并存；只作为 diagnostic info，不作为默认 A2 policy / lowlevel backend topic。
- [x] 更新 `a2_real_robot_test.sh`、`A2_REAL_ROBOT_TEST.md`、`README.md`、`A2_DOCKER_BUILD_TEST.md`，使 connected preflight 校验 configured topic type，并把 `no-lowcmd` 放到 real robot publish path 前。

## 2026-06-05 21:52 HKT

- [x] 修复 A2 policy listen-only safety P1：`A2PolicyDeployNode` remote local stop 仍执行 `set_zero_command()` 和 `reset_runtime_state()`，但只有 `enable_motion=true` 时才 `publish_zero()`；`enable_motion=false` 下明确 log runtime reset 且不发布 zero LowCmd。
- [x] 更新 `ros2/A2/README.md` 和 `ros2/A2/scripts/A2_REAL_ROBOT_TEST.md`：`policy-listen-remote` 继续是 no-lowcmd observe/listen-only，按 `Select` / `L2+B` 也不发布；`enable_motion=true` 阶段 local stop 才发布 zero LowCmd。

## 2026-06-05 22:03 HKT

- [x] 新增 `a2_real_robot_observer.py joints-live` observe-only subcommand：只订阅 configured lowstate topic（默认 `/lowstate`），duration 默认 `0` 表示 Ctrl-C live mode，每 `--print-period` 秒打印固定 12 joint label 的 `q`、`dq`、`delta_from_start`、`range` 和 `*` changed marker。
- [x] 新增 `a2_real_robot_observer.py remote-live` observe-only subcommand：只订阅 configured lowstate topic（默认 `/lowstate`），每 `--print-period` 秒打印 raw sticks、deadzone/clamped display sticks、pressed buttons 和 valid flag。
- [x] `a2_real_robot_test.sh` 新增 `joints-live` / `remote-live` wrapper，并暴露 `A2_LIVE_PRINT_PERIOD`、`A2_LIVE_CLEAR_SCREEN`、`A2_JOINT_MIN_DELTA`、`A2_REMOTE_DEADZONE`。
- [x] 更新 `A2_REAL_ROBOT_TEST.md` 和 `README.md`：人工 joint mapping / remote decode 优先使用 live observe-only tools，旧 `joints` / `remote` 保留为 summary、CSV 或 pass/fail validation。
- [x] 更新 A2 memory TODO：部署机/实机下一步先用 `joints-live` / `remote-live` 完成人工 mapping/decode 验证，再进入后续 guarded publish path。

## 2026-06-05 22:20 HKT

- [x] 修复 `a2_real_robot_test.sh` 中 MotionSwitcher helper 的手写 `g++` compile path：自动去重打印 SDK2 include dirs / lib dirs，覆盖 `install/include/ddscxx`、`install/include/ddsc`、`thirdparty/include/ddscxx`、`thirdparty/include/ddsc`、`install/lib`、`thirdparty/lib/$(uname -m)` 等部署机 SDK2 DDS layout candidates。
- [x] MotionSwitcher helper compile 显式链接 `-lunitree_sdk2 -lddscxx -lddsc -pthread`，降低 header 修复后继续出现 DDS unresolved symbols 的风险。
- [x] `motion-check` 仍保持 observe-only，只调用 `CheckMode`；`motion-release` 仍保持 `A2_ALLOW_RELEASE_MODE=1` env guard。helper compile 失败时脚本删除可能残留的 helper binary 并停止，不继续执行不存在的 helper。
- [x] 更新 `A2_REAL_ROBOT_TEST.md` 和 `README.md`，记录 `TopicTraits.hpp` nested `ddscxx` include path 问题、DDS lib dirs、验证 `find` 命令和 compile smoke command。

## 2026-06-05 22:32 HKT

- [x] 根据部署机 `motion-check` 运行期 `free(): invalid pointer` 加固 MotionSwitcher helper runtime：生成 wrapper 并在执行 helper 前将 SDK2 lib dirs 前置到 `LD_LIBRARY_PATH`，避免已 source ROS2 Humble 后 ROS2/CycloneDDS `libddsc*.so` shadow SDK2 bundled DDS libs。
- [x] `motion-check` helper compile 后打印 `ldd` 结果，便于确认 `libunitree_sdk2`、`libddscxx`、`libddsc` 解析到 `/opt/unitree/unitree_sdk2/install/lib` 或 `thirdparty/lib/$(uname -m)`。
- [x] helper C++ 增加阶段日志：`ChannelFactory::Init`、`MotionSwitcherClient::Init`、`CheckMode`、`ChannelFactory::Release`；成功路径显式调用 `ChannelFactory::Release()`。
- [x] 更新 `A2_REAL_ROBOT_TEST.md`、`README.md` 和 A2 memory TODO，要求部署机复跑 `motion-check` 并回传 `ldd` / stage log；`motion-check` 仍不发布 LowCmd、不接入 policy。

## 2026-06-05 22:54 HKT

- [x] 实现 A2 Stand-Up + Policy Handover gate：`a2_policy_deploy` 默认 `require_standup_before_policy=true`，`enable_motion=true` / `command_source=remote` 下按 `IdleBlocked -> StandUpInterpolating -> StandHoldWaitingForA -> PolicyWarmupHold -> PolicyActive` 执行 two-A handover。
- [x] stand-up / holder / warmup command 全部只构造 `A2JointCommand` 并调用 `A2LowLevelInterface::publish_joint_commands()`；没有直接写 `unitree_hg::msg::LowCmd`，继续保留 fresh-state、mode routing 和 CRC boundary。
- [x] 新增 stand-up params：`standup_stage1_steps=150`、`standup_stage2_steps=150`、`standup_rear_alpha_lead=0.10`、`standup_front_alpha_lag=0.04`、`standup_kp_start=3.0`、`standup_kd_start=0.5`、`standup_final_gain_scale=1.0`，并在 runtime refresh 中校验 invalid params。
- [x] remote safety 更新：`Select` / `L2+B` 在任意 phase local stop；stand-up / holder / warmup 阶段 `B` rising edge cancel；`enable_motion=false` 下不发布 zero LowCmd，`enable_motion=true` 下才发布 zero LowCmd。
- [x] 更新 `a2_real_robot_test.sh` `policy-enable-remote` warning、`A2_REAL_ROBOT_TEST.md` Section 10 / acceptance checklist、`README.md` policy/safety sections 和 A2 memory TODO。

## 2026-06-05 23:18 HKT

- [x] 取消 A2 policy `L2` locomotion gate：A2 R3 `L2` decode 实机不可靠，`A2PolicyDeployNode::update_command_from_remote()` 不再因 `remote.buttons.l2=false` 强制 command zero，也不再输出 `L2 gate is not held`。
- [x] stand-up second-`A` handover 不再要求 `L2` release；仍要求 `lx/rx/ly` sticks 在 deadzone 后为 zero，才进入 `PolicyWarmupHold`。
- [x] 保留 local stop safety：`Select` 是 primary local stop；`L2+B` 仅保留为附加 stop path；stand-up / hold / warmup 阶段 `B` rising edge cancel 不变。
- [x] 更新 `a2_real_robot_test.sh` `policy-enable-remote` warning、`A2_REAL_ROBOT_TEST.md`、`README.md` 和 A2 memory TODO/DONE/description，移除旧 `L2` gate 文案。

## 2026-06-05 23:38 HKT

- [x] 新增 A2 内置 motion service guarded restore/select：`a2_real_robot_test.sh motion-select IFACE MODE` 要求 `A2_ALLOW_SELECT_MODE=1`，调用 `MotionSwitcherClient::SelectMode(MODE)`，并在 Select 前后 `CheckMode`。
- [x] 新增 `motion-restore IFACE`，默认恢复 `A2_MOTION_RESTORE_MODE:-ai_sport`；初版成功条件为 `SelectMode ret==0` 且 after `CheckMode ret==0 name==mode`，该 raw-name exact check 已在 2026-06-08 21:08 HKT 被 normalized `service` alias 判定 supersede。
- [x] 保留 `motion-check` observe-only 和 guarded `motion-release` 行为，未引入 LowCmd publish、policy attach 或 `a2_policy_deploy` 修改。
- [x] 更新 `A2_REAL_ROBOT_TEST.md` 和 `README.md`，要求恢复前停止 policy/LowCmd publisher、运行 `no-lowcmd` pass，并给出部署机内 restore 命令与 Unitree App fallback。
- [x] 更新 A2 memory TODO/description：关闭和恢复内置 service 已有 guarded script，但实机流程仍需 operator 执行和验证。

## 2026-06-06 00:03 HKT

- [x] 新增 operator-facing `ros2/A2/scripts/A2_REAL_DEPLOY_RUNBOOK.md`，作为 day-to-day A2 Docker real deployment operation doc，不替代 `A2_REAL_ROBOT_TEST.md` validation/reference guide。
- [x] Runbook 覆盖 host cold start、Docker image/container、A2 `192.168.123.x` network、container env、workspace build/source、connected readiness、MotionSwitcher guarded release/restore、policy listen-only gate、guarded `enable_motion=true` two-A handover、runtime stop、disconnect 和 failure log collection。
- [x] 更新 `ros2/A2/README.md`，新增 runbook 入口并明确 `A2_REAL_ROBOT_TEST.md` 仍用于 validation/reference。
- [x] 更新 A2 memory description/TODO/DONE，记录 daily deployment runbook 已存在，但 broad real validation 和每次 operator safety checks 仍未因此完成。

## 2026-06-06 00:12 HKT

- [x] 将 A2 remote command 上限统一调整为 `max_remote_vx=0.8`、`max_remote_vy=0.5`、`max_remote_yaw=0.6`。
- [x] 更新 `a2_policy_deploy` 默认参数、`policy-enable-remote` wrapper 参数、README、real robot validation guide、real deployment runbook 和 A2 memory。

## 2026-06-08 20:08 HKT

- [x] 新增默认 A2 policy remote run config `ros2/A2/config/a2_policy_remote.env`，记录 remote caps、deadzone、stand-up gate 和 aux monitor defaults；`A2_ALLOW_ENABLE_MOTION=1` 明确不写入 config。
- [x] `a2_real_robot_test.sh` policy subcommands 新增加载 config：默认读取 repo config，可用 `A2_POLICY_RUN_CONFIG` 覆盖；优先级为 script defaults < config file < operator `A2_POLICY_*` env；brake gate fields 只打印为 ignored/comment-only，不传给 active behavior。
- [x] 新增 `policy-aux-live` wrapper：`enable_motion=false command_source=remote monitor_policy_aux=true`，同时启动 `no-lowcmd` observer；duration `0` 表示持续运行直到 Ctrl-C，finite duration 下 observer 覆盖收尾窗口。
- [x] `a2_policy_deploy` 新增 aux monitor params：`monitor_policy_aux`、`policy_aux_expected_dim`、`policy_aux_print_period_sec`；history warm 后可在 `enable_motion=false` 下执行 inference-only aux monitor，不发布 LowCmd。
- [x] aux monitor 打印 action dim、aux dim 和 values；aux dim `6` 按 Aliengo convention 输出 `pred_base_lin_vel[0..2]` 与 `pred_base_force_local[0..2]`，aux empty / dim mismatch / NaN/Inf 都有明确 log。
- [x] 更新 `A2_REAL_DEPLOY_RUNBOOK.md`、`A2_REAL_ROBOT_TEST.md` 和 `README.md`，记录 run config、`policy-aux-live`、listen-only/no-lowcmd safety，以及 brake gate 暂不启用。

## 2026-06-08 20:31 HKT

- [x] `a2_policy_deploy` 新增 active aux debug publisher：`publish_aux_debug` 默认由 wrapper config 开启，`aux_debug_topic` 默认 `/a2/policy_aux`，每次 `computeAction()` 后发布 `policys[kPolicyId].get_last_aux_output()` 的 aux vector。
- [x] `enable_motion=false` early return 已扩展为仅在 `monitor_policy_aux=false` 且 `publish_aux_debug=false` 时跳过 inference；因此 `policy-listen-remote` 可以在 no-LowCmd 前提下发布 `/a2/policy_aux`。
- [x] 新增 `a2_real_robot_observer.py policy-aux-topic-live` 和 wrapper `policy-aux-monitor [duration]`，只订阅 `std_msgs/msg/Float32MultiArray`，打印 sample count、age、dim、first8 values 和 dim 6 force-estimator layout warning。
- [x] 更新 `a2_policy_remote.env`、`a2_real_robot_test.sh`、`A2_REAL_DEPLOY_RUNBOOK.md`、`A2_REAL_ROBOT_TEST.md` 和 `README.md`，记录 `policy-aux-live` 是 independent listen-only smoke，`policy-aux-monitor` 是 active policy aux topic subscriber。

## 2026-06-08 21:08 HKT

- [x] 加固 `a2_real_robot_test.sh` 生成的 MotionSwitcher helper：按 Unitree SDK2 sample 对 `CheckMode form/name` 做 normalized `service` 输出，至少覆盖 `form='0'` 下 `normal->sport_mode`、`ai->ai_sport`、`advanced->advanced_sport`，并保留 wheel aliases / unknown fallback。
- [x] `motion-select` / `motion-restore` 成功条件改为 `SelectMode ret==0` 且 after `CheckMode` raw `name` 等于 target mode 或 normalized `service` 等于 target mode；仍不接受任意非空 mode，错误信息同时打印 after raw `name` 和 normalized `service`。
- [x] 更新 `A2_REAL_ROBOT_TEST.md`、`A2_REAL_DEPLOY_RUNBOOK.md` 和 `README.md` restore expected output，记录 `form='0', name='ai', service='ai_sport'` 是恢复 `ai_sport` 的 expected alias。
