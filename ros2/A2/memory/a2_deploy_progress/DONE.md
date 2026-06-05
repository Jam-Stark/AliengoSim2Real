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
- [x] `a2_policy_deploy` 增加 `command_source=static|remote`，保留 static command params，并实现 remote mapping、`L2` gate、`Select` / `L2+B` local stop、runtime reset 和 `enable_motion=true` zero stop。
- [x] 更新 `ros2/A2/README.md` remote build/run、button gate、joystick mapping、safety limits 和 deploy machine validation checklist。

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
