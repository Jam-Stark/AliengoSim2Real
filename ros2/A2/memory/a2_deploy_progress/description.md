---
name: a2_deploy_progress
scope: ros2/A2
status: active
last_updated: "2026-06-05 18:41 HKT"
owned_paths:
  - ros2/A2/
  - ros2/A2_Guide/
read_when:
  - 修改 A2 ROS2 low-level deployment、A2 policy deploy、unitree_hg interface、rt/lowstate/rt/lowcmd routing 或 deploy machine readiness 时
---

## Purpose

本 entry 记录 `ros2/A2` 的独立 A2 ROS2 low-level deployment 起点。当前 A2 work 不修改既有 Go2W `ros2/src/**` 链路。

已完成事实：

- 新建独立 ament package `a2_lowlevel`。
- 实现标准 A2 12-motor low-level adapter：订阅 `rt/lowstate`，发布 `rt/lowcmd`，使用 `unitree_hg` ROS2 messages。
- 实现 `A2LowLevelInterface` public API：`latest_state()`、`has_fresh_state()`、`publish_zero()`、`publish_joint_commands()`。
- 实现 A2 专属 CRC，未复用 Go2W `motor_crc`。
- 实现 `a2_lowlevel_smoke`，默认 listen-only，`publish_zero` / `stand_test` 需要显式参数。
- 实现可选 `a2_policy_deploy` target：`BUILD_A2_POLICY_DEPLOY=ON` 时才查找 LibTorch/jsoncpp，并通过 shared `ManagerBasedEnv` / `Policy` runtime 加载 `policy/A2_policy/policy.pt`。
- `a2_policy_deploy` 校验 `policy/A2_policy/policy.json` contract：`action_dim=12`、`per_frame_obs_dim=46`、`history_length=32`、`action_scale=0.25`、`sim_dt=0.005`、`control_decimation=4`、训练 joint order。
- A2 policy observation 每帧为 `projected_gravity_xy(2)`、`base_ang_vel(3)*0.25`、`joint_q-default_pos(12)`、`joint_dq*0.05`、`last_raw_action(12)`、`gait_clock(2)`、`command(3)*[2,2,0.25]`，history `32` flatten 为 `1472`。
- A2 policy action 先按 `action_clip` clip，再映射为 `default_joint_pos + action_scale * raw_action`；训练顺序到 A2 low-level order same signs / no inversion，低层发布只走 `A2LowLevelInterface::publish_joint_commands()`。
- 实现 A2 Remote Control v1 decode contract：`wireless_remote[40]` 中 `lx/rx/ry/ly` 分别来自 offsets `4/8/12/20` 的 little-endian `float32`，button byte `2/3` 按 Unitree SDK2 sample bit order decode，并做 `deadzone`、`[-1,1]` clamp 和 NaN/Inf invalid guard。
- `a2_policy_deploy` 支持 `command_source=static|remote`，默认 `static`；remote mapping 为 `cmd_vx=ly*max_remote_vx`、`cmd_vy=-lx*max_remote_vy`、`cmd_yaw=-rx*max_remote_yaw`，`L2` 是 nonzero command gate，`Select` 或 `L2+B` 触发 local stop / `publish_zero()` / policy runtime reset。
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

当前 blocker：

- code machine 没有 ROS2 / `colcon` / `/opt/ros`，无法本地完整 build `a2_lowlevel`。
- code machine 是 macOS，但本 thread 明确不跑 Docker Desktop offline validation；真实 DDS/network/control 仍需在 Linux deploy machine + A2 网络上验证。
- 部署机 host 是 Ubuntu 24.04.3，因此 A2 deploy 采用 Ubuntu 22.04 + ROS2 Humble Docker container；不要要求 host 原生安装 Humble。
- A2 CRC 仍需和部署机 `unitree_hg` generated messages、Unitree SDK2 sample 或实机 low-level command 行为对照验证。
- A2 R3 remote layout 已按 Unitree SDK2 sample 实现，但仍需在部署机/实机用真实 `rt/lowstate.wireless_remote[40]` 验证 stick/button 方向、`L2` gate 和 local stop button。
- 低层实机控制前必须确认 Unitree 内置运动服务 `ai_sport` / `ai_sports` 已关闭；当前首版只在 README / smoke log 提醒，未自动调用 `MotionSwitcherClient`。

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

- 在部署机 build A2 Docker image，并运行 `ros2/A2/docker/preflight.sh --iface enp131s0 --container-check` 验证 ROS2 Humble / Unitree ROS2 message readiness。
- 手动配置部署机 `enp131s0` 到 `192.168.123.99/24`，确认 A2 `192.168.123.x` low-level subnet 连通；不要把 `192.168.124.x` 当作 SDK2 low-level subnet。
- 在 Docker container 内 build `a2_lowlevel`，验证 `unitree_hg` include/type/field names 和 CRC。
- 首次实机前确认 `ai_sport` / `ai_sports` 关闭、离地或限功率 smoke、hardware emergency stop。
- 在 Docker container 内用 CPU LibTorch `/opt/libtorch` 和 `-DBUILD_A2_POLICY_DEPLOY=ON` build `a2_policy_deploy` 并验证 TorchScript runtime。
- 在部署机/实机验证 A2 R3 remote layout：`a2_lowlevel_smoke log_remote` stick/button decode、`a2_policy_deploy command_source=remote` 的 `L2` gate、`Select` / `L2+B` local stop 和 mapping 方向。
- 按 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md` 在部署机执行 Docker build/preflight/offline smoke，并回填实际 pass/fail。

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

## Recommended Next Files To Read

- `ros2/A2/README.md`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/memory/a2_deploy_progress/TODO.md`
- `ros2/A2/memory/a2_deploy_progress/DONE.md`
- `memory/shared_policy_runtime/description.md`
