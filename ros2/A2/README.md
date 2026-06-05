# a2_lowlevel

`a2_lowlevel` 是独立的 ROS2 ament package，用于标准 A2 low-level adapter。默认只 build A2 12 个 leg motors 的 low-level library 和 smoke，不修改 `ros2/src/**` 的 Go2W 链路。A2 policy deploy 是可选 target，需要显式打开 `BUILD_A2_POLICY_DEPLOY`。

## Build

先确保 Unitree SDK2 / ROS2 环境已经提供并 source 了 `unitree_hg` generated messages。

```bash
cd /Users/caobaoquan/Downloads/python/projects/AliengoSim2Real/ros2
colcon build --packages-select a2_lowlevel
source install/setup.bash
```

只 build low-level adapter / smoke 时不需要 LibTorch 或 jsoncpp。

## Optional Policy Build

`a2_policy_deploy` 通过 shared `utils/cpp_manager_env` 的 `ManagerBasedEnv` / `Policy` runtime 加载 TorchScript policy，但 low-level publish boundary 仍然只走 `A2LowLevelInterface::publish_joint_commands()`。

启用 policy target：

```bash
cd /Users/caobaoquan/Downloads/python/projects/AliengoSim2Real/ros2
colcon build --packages-select a2_lowlevel --cmake-args \
  -DBUILD_A2_POLICY_DEPLOY=ON
source install/setup.bash
```

如果 policy asset 不在默认位置，可覆盖 CMake default 或 ROS params：

```bash
colcon build --packages-select a2_lowlevel --cmake-args \
  -DBUILD_A2_POLICY_DEPLOY=ON \
  -DA2_POLICY_DEFAULT_PATH=/path/to/policy.pt \
  -DA2_POLICY_DEFAULT_JSON_PATH=/path/to/policy.json
```

默认 policy contract:

- TorchScript: `policy/A2_policy/policy.pt`
- JSON: `policy/A2_policy/policy.json`
- deploy control rate: `50 Hz`

## Docker Deployment

`ros2/A2/docker/` 提供 A2 专用 Docker deployment layer。目标是让部署机即使是 Ubuntu 24.04，也通过容器固定到 Ubuntu 22.04 + ROS2 Humble + apt CycloneDDS/RMW。A2 package 仍然只依赖 ROS2 `unitree_hg` messages；Unitree SDK2 只在镜像中 build/install official examples，用于 MotionSwitcherClient / SDK smoke，不作为 `a2_lowlevel` 的直接依赖。

A2 Docker 的 official target platform 是 `linux/amd64`，因为当前正式部署机是 x86_64。`build_image.sh` 和 `run_container.sh` 默认都会传入 `--platform linux/amd64`；只在 debug 特殊架构时使用 `A2_DOCKER_PLATFORM=linux/arm64` 等覆盖。Apple Silicon Mac 上的 Docker Desktop 可以通过 amd64 emulation build/run 大部分 offline validation，但性能、timing、host networking 和 DDS 行为不代表真实部署机；真实 A2 DDS/network/control 仍然只在部署机 + 机器人网络上验证。

镜像内容：

- base image: `ros:humble-ros-base-jammy`
- ROS2/RMW: `ros-humble-rmw-cyclonedds-cpp`、`ros-humble-rosidl-generator-dds-idl`
- build deps: `python3-colcon-common-extensions`、`build-essential`、`cmake`、`libyaml-cpp-dev`、`libjsoncpp-dev`
- debug tools: `iproute2`、`iputils-ping`、`net-tools`、`dnsutils`、`tcpdump`、`ethtool`
- CPU LibTorch: `/opt/libtorch`，默认 `cxx11-abi shared-with-deps CPU zip`
- Unitree refs:
  - `unitree_ros2@5204e6e`
  - `unitree_sdk2@63c6f53`
  - `unitree_sdk2_python@f7a5526`

Build image on host:

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
bash ros2/A2/docker/build_image.sh
```

默认 image tag 是 `a2-humble-deploy:2026-06-05`，默认 platform 是 `linux/amd64`。二者都可覆盖：

```bash
A2_DOCKER_IMAGE=a2-humble-deploy:local \
A2_DOCKER_PLATFORM=linux/amd64 \
bash ros2/A2/docker/build_image.sh
```

Run container on host:

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
A2_NET_IFACE=lo bash ros2/A2/docker/run_container.sh
```

默认 host mount 使用部署机报告中的路径：

```text
/home/baoquanc/Downloads/WorkSpace/projects -> /work/projects
```

如本机 workspace 不同，可覆盖：

```bash
HOST_PROJECTS_DIR=/path/to/projects \
A2_NET_IFACE=lo \
bash ros2/A2/docker/run_container.sh
```

## Mac Docker Desktop Offline Validation

Apple Silicon Mac 可用 Docker Desktop 的 amd64 emulation 做 offline validation。先启动 Docker Desktop，等待 `docker info` 可用，然后在本机 workspace 运行：

```bash
cd /Users/caobaoquan/Downloads/python/projects/AliengoSim2Real
bash ros2/A2/docker/build_image.sh
```

进入 amd64 emulated container，offline 使用 loopback interface：

```bash
HOST_PROJECTS_DIR=/Users/caobaoquan/Downloads/python/projects \
A2_NET_IFACE=lo \
bash ros2/A2/docker/run_container.sh bash
```

在 container 内 build low-level 和 policy target：

```bash
/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
```

做 interface/package checks 和 listen-only smoke：

```bash
ros2 pkg prefix unitree_hg
ros2 interface show unitree_hg/msg/LowState >/dev/null
ros2 interface show unitree_hg/msg/LowCmd >/dev/null
ros2 pkg prefix a2_lowlevel
timeout 3 ros2 run a2_lowlevel a2_lowlevel_smoke || true
```

Mac offline validation 只能覆盖 Docker image、workspace build、ROS2 message availability、node startup 和 pure listen-only smoke。`--network host` 在 Docker Desktop 上不等价于 Linux host networking；A2 `enp131s0`、`192.168.123.x` DDS traffic、remote decode with real ROS2 visible `/lowstate`、policy timing 和任何 low-level command/control 都必须回到部署机验证。

`run_container.sh` 使用：

- `--network host`
- `--privileged`
- `--ipc host`
- `-v "$HOST_PROJECTS_DIR:/work/projects"`

Container entrypoint 会 source：

- `/opt/ros/humble/setup.bash`
- `/opt/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash`
- `/work/projects/AliengoSim2Real/ros2/install/setup.bash`，如果已经 build

并设置：

- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`
- `Torch_DIR=/opt/libtorch/share/cmake/Torch`
- `LD_LIBRARY_PATH=/opt/libtorch/lib:$LD_LIBRARY_PATH`
- `CYCLONEDDS_URI`，由 `A2_NET_IFACE` 自动生成，默认 `lo`

真实 A2 硬件必须显式设置机器人 Ethernet NIC，例如部署机报告中的 `enp131s0`：

```bash
A2_NET_IFACE=enp131s0 bash ros2/A2/docker/run_container.sh
```

## Docker Workspace Build

进入容器后，从 `/work/projects/AliengoSim2Real/ros2` build A2 package：

```bash
/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
```

Build optional policy target：

```bash
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
```

Policy build 等价于：

```bash
cd /work/projects/AliengoSim2Real/ros2
colcon build --packages-select a2_lowlevel --cmake-args \
  -DBUILD_TESTING=OFF \
  -DBUILD_A2_POLICY_DEPLOY=ON \
  -DTorch_DIR=/opt/libtorch/share/cmake/Torch
```

## Docker Preflight

Host preflight 不会修改网络，只检查 Docker、candidate NIC、A2 subnet、workspace mount 和可选 container readiness：

Preflight 默认报告并在 `--container-check` 时使用 `A2_DOCKER_PLATFORM=linux/amd64`，可用 `--platform` 或 env override；部署机 default 应保持 amd64。

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
bash ros2/A2/docker/preflight.sh --iface enp131s0
```

可选 ping A2 low-level address：

```bash
bash ros2/A2/docker/preflight.sh --iface enp131s0 --ping
```

可选进入镜像检查 Unitree ROS2 messages：

```bash
bash ros2/A2/docker/preflight.sh --iface enp131s0 --container-check
```

如果 `enp131s0` 没有 `192.168.123.x`，手动配置示例：

```bash
sudo ip link set enp131s0 up
sudo ip addr flush dev enp131s0
sudo ip addr add 192.168.123.99/24 dev enp131s0
```

`192.168.124.x` 不是当前 SDK2 low-level DDS chain 使用的 subnet；official DDS reference names `rt/lowstate` / `rt/lowcmd` 应走 `192.168.123.x`，当前 ROS2 backend 默认 visible topics 是 `/lowstate` / `/lowcmd`。

## Docker Smoke Runs

Offline smoke，不连接实机，只验证 package、message 和 node 可启动：

```bash
A2_NET_IFACE=lo bash ros2/A2/docker/run_container.sh bash
/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 pkg prefix unitree_hg
ros2 pkg prefix a2_lowlevel
timeout 3 ros2 run a2_lowlevel a2_lowlevel_smoke || true
```

Connected listen-only smoke：

```bash
A2_NET_IFACE=enp131s0 bash ros2/A2/docker/run_container.sh bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 topic echo /lowstate --once
ros2 run a2_lowlevel a2_lowlevel_smoke
```

Remote logging，仍然 listen-only、不发布 command：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
  -p log_remote:=true \
  -p remote_deadzone:=0.08
```

Policy remote run，默认仍需显式 `enable_motion=true`，且 `L2` gate 控制 nonzero command：

```bash
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=true \
  -p command_source:=remote \
  -p max_remote_vx:=0.4 \
  -p max_remote_vy:=0.25 \
  -p max_remote_yaw:=0.6
```

Unitree SDK2 smoke binaries are built in `/opt/unitree/unitree_sdk2/build/bin` inside the image. Use them only as official SDK diagnostics, for example checking SDK2 topic/service readiness before low-level A2 policy testing. They are not linked into `a2_lowlevel`.

## Docker Migration

保存镜像：

```bash
docker save a2-humble-deploy:2026-06-05 | gzip > a2-humble-deploy_2026-06-05.tar.gz
```

导入镜像：

```bash
gunzip -c a2-humble-deploy_2026-06-05.tar.gz | docker load
```

也可以用 `docker tag` 标记稳定版本：

```bash
docker tag a2-humble-deploy:2026-06-05 a2-humble-deploy:validated-on-lt5
```

## Deploy Machine Info

调整 A2 deployment chain 前，建议先在真实部署机上采集一次机器、网络、ROS2、Unitree repo 和 A2 package readiness 信息。该报告用于判断 deploy machine 的 ROS2 distro、CycloneDDS/RMW 配置、Unitree SDK2/ROS2 checkout 状态、`unitree_hg` interfaces、网卡网段和基础 build/runtime tools 是否满足 A2 low-level chain 的要求。

当前收到的 `ros2/A2/DeployMachineINFO.md` 显示部署机为 `lt5.precognition.team` / user `baoquanc`，host OS 是 Ubuntu 24.04.3，host 未安装 `/opt/ros`，Unitree repos 已在 `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree`，并且 refs 符合 Dockerfile pin。Docker deployment 因此固定使用 Ubuntu 22.04 + ROS2 Humble container，而不是要求 host 原生安装 Humble。

默认输出 Markdown 到 stdout，可重定向保存为 `ros2/A2/DeployMachineINFO.md`：

```bash
cd /path/to/AliengoSim2Real
bash ros2/A2/scripts/collect_deploy_machine_info.sh > ros2/A2/DeployMachineINFO.md
```

如果部署机已连接 Unitree/A2 网络，可额外执行短 ping 检测 `192.168.123.161`、`192.168.123.162`、`192.168.124.162`：

```bash
bash ros2/A2/scripts/collect_deploy_machine_info.sh --ping > ros2/A2/DeployMachineINFO.md
```

如果 Unitree sources 不在默认 `$HOME/third_party/unitree`，显式指定 root：

```bash
bash ros2/A2/scripts/collect_deploy_machine_info.sh \
  --unitree-root /path/to/unitree \
  > ros2/A2/DeployMachineINFO.md
```

脚本默认启用 `--no-sensitive` 行为，不 dump 全量 env；只输出 `ROS_DISTRO`、`RMW_IMPLEMENTATION`、`CYCLONEDDS_URI` 的受限摘要和必要 command/path/version 信息。采集失败的 probe 会标记为 `MISSING` / `UNAVAILABLE` / `FAILED` 并继续，适合在非 ROS 或 macOS 环境先做 smoke。

## Topic / Type

- Subscribe default: `lowstate` parameter value, visible as `/lowstate` in the ROS2 graph
- Type: `unitree_hg/msg/LowState`
- Publish default: `lowcmd` parameter value, visible as `/lowcmd` in the ROS2 graph
- Type: `unitree_hg/msg/LowCmd`

A2_Guide / Unitree DDS reference names 是 `rt/lowstate` / `rt/lowcmd`。当前部署机 real A2 的 ROS2 graph 可见 topic 是 `/lowstate` / `/lowcmd`，因此 `A2LowLevelInterface` 默认参数为 `lowstate_topic=lowstate`、`lowcmd_topic=lowcmd`。如现场 bridge 或 wrapper 暴露不同名字，可通过 ROS2 parameters 修改：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
  -p lowstate_topic:=/rt/lowstate \
  -p lowcmd_topic:=/rt/lowcmd
```

`A2LowLevelInterface` 会保存最近一次 `LowState` 的 `mode_pr`、`mode_machine`、`tick`、IMU quaternion/gyroscope、前 12 个 joint q/dq 和 `wireless_remote[40]`。发送非零 joint command 时必须先收到 fresh configured lowstate topic，默认 `/lowstate`，否则拒绝发布并 log warn。

## 12 Motor Order

标准 A2 12 motor order 固定为：

| Index | Name |
| --- | --- |
| 0 | `FR_BODY` |
| 1 | `FR_THIGH` |
| 2 | `FR_CALF` |
| 3 | `FL_BODY` |
| 4 | `FL_THIGH` |
| 5 | `FL_CALF` |
| 6 | `RR_BODY` |
| 7 | `RR_THIGH` |
| 8 | `RR_CALF` |
| 9 | `RL_BODY` |
| 10 | `RL_THIGH` |
| 11 | `RL_CALF` |

代码中提供 `A2MotorIndex`、`kA2MotorOrder`、`kA2MotorNames` 常量。`publish_joint_commands()` 只写 `motor_cmd[0:12]`，`motor_cmd[12:35]` 会清零。

## A2 Policy Contract

`a2_policy_deploy` 启动时会读取并校验 `policy.json`：

- `action_dim = 12`
- `per_frame_obs_dim = 46`
- `history_length = 32`
- flattened observation dim = `1472`
- `action_scale = 0.25`
- `sim_dt = 0.005`
- `control_decimation = 4`
- `1 / (sim_dt * control_decimation) = 50 Hz`
- `joint_names` 和 `obs_joint_names` 必须等于训练顺序：
  `FL_hip, FR_hip, RL_hip, RR_hip, FL_thigh, FR_thigh, RL_thigh, RR_thigh, FL_calf, FR_calf, RL_calf, RR_calf`

每帧 observation order 固定为：

| Segment | Dim | Scale |
| --- | ---: | --- |
| `projected_gravity_xy` | 2 | 1 |
| `base_ang_vel` | 3 | `0.25` |
| `joint_q - default_pos` | 12 | 1 |
| `joint_dq` | 12 | `0.05` |
| `last_raw_action` | 12 | 1 |
| `gait_clock` | 2 | 1 |
| `command` | 3 | `[2, 2, 0.25]` |

History length 是 `32`，通过 `ManagerBasedEnv` observation terms 展平。`a2_policy_deploy` 不让 policy 直接写 `unitree_hg::msg::LowCmd`；policy output 先映射成 `std::array<A2JointCommand, 12>`，再交给 `A2LowLevelInterface` 处理 fresh-state guard、mode routing 和 CRC。

Command provider 可选 `static` 或 `remote`，最终进入 observation 的 command 仍按 `[2, 2, 0.25]` scale。gait clock 使用未 scale 的 active command 判断 standing：`abs(cmd_vx) < 0.1`、`abs(cmd_vy) < 0.1`、`abs(cmd_yaw) < 0.2` 时 gait phase reset/保持为 `0`，gait clock 为 `[0, 1]`；非 standing command 才按 `gait_frequency_hz / control_hz` 前进。

## A2 Remote Decode Contract

`a2_remote` 从 `wireless_remote[40]` decode A2 R3 remote state。stick 使用 Unitree SDK2 sample layout，全部是 little-endian `float32`：

| Stick | Offset |
| --- | ---: |
| `lx` | 4 |
| `rx` | 8 |
| `ry` | 12 |
| `ly` | 20 |

Button layout：

| Byte | Bits |
| --- | --- |
| `2` | `R1`, `L1`, `Start`, `Select`, `R2`, `L2`, `F1`, `F3` |
| `3` | `A`, `B`, `X`, `Y`, `Up`, `Right`, `Down`, `Left` |

Decode 后会先做 NaN/Inf guard；任何 stick float 非 finite 时 `A2RemoteState.valid=false`，remote stick 不参与 policy command。finite stick 会按 `remote_deadzone` 置零 deadzone 内输入，并 clamp 到 `[-1, 1]`。

## Policy Action Mapping

Policy raw action 使用训练 joint order。发送前按 `policy.json` 的 `action_clip` clip，然后转换为 position target：

```text
target_q = default_joint_pos + action_scale * clipped_raw_action
```

训练顺序到 A2 low-level order 的 mapping 固定为 same signs、no inversion：

| Training Joint | A2 Low-Level Joint |
| --- | --- |
| `FL_hip_joint` | `FL_BODY` |
| `FR_hip_joint` | `FR_BODY` |
| `RL_hip_joint` | `RL_BODY` |
| `RR_hip_joint` | `RR_BODY` |
| `FL_thigh_joint` | `FL_THIGH` |
| `FR_thigh_joint` | `FR_THIGH` |
| `RL_thigh_joint` | `RL_THIGH` |
| `RR_thigh_joint` | `RR_THIGH` |
| `FL_calf_joint` | `FL_CALF` |
| `FR_calf_joint` | `FR_CALF` |
| `RL_calf_joint` | `RL_CALF` |
| `RR_calf_joint` | `RR_CALF` |

PD gains 按 joint type 固定：

| Joint Type | `kp` | `kd` |
| --- | ---: | ---: |
| hip/body | 140 | 5 |
| thigh | 140 | 5 |
| calf | 220 | 9 |

`dq=0`、`tau=0`。

## Public API

- `A2LowStateSnapshot latest_state() const`
- `bool has_fresh_state(std::chrono::milliseconds timeout) const`
- `bool publish_zero()`
- `bool publish_joint_commands(const std::array<A2JointCommand, 12>& commands)`

`publish_joint_commands()` 使用最近 `LowState` 的 `mode_pr` 和 `mode_machine`，并要求 state age 不超过 `state_timeout_ms`，默认 `200 ms`。任何带非零 `q/dq/tau/kp/kd` 的 joint command 会强制使用 FOC `mode=0x01`。`publish_zero()` 可以在无 state 时发布安全 zero/stop command；无 state 时 `mode_pr=0`、`mode_machine=0`，有 state 时跟随最新 state。zero command 使用 `mode=0x00`，并且 `q/dq/tau/kp/kd` 全部为 `0`。

## Smoke Run

默认只监听，每秒打印 tick/mode/joints，不发布命令：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke
```

可选打印 remote decode，仍然不发布命令：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
  -p log_remote:=true \
  -p remote_deadzone:=0.08
```

低频发布 zero/stop command：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p publish_zero:=true
```

发布固定低刚度站立目标：

```bash
ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p stand_test:=true
```

参数：

- `publish_zero`：默认 `false`。为 `true` 时按 `command_hz` 发布 zero/stop command。
- `stand_test`：默认 `false`。为 `true` 时只在 fresh state 下发布固定低刚度站立目标；若同时设置 `publish_zero`，`stand_test` 优先。
- `state_timeout_ms`：默认 `200`。用于 fresh state 判断。
- `lowstate_topic`：默认 `lowstate`，ROS2 graph 通常显示为 `/lowstate`。
- `lowcmd_topic`：默认 `lowcmd`，ROS2 graph 通常显示为 `/lowcmd`。
- `command_hz`：默认 `20`。用于 `publish_zero` 或 `stand_test` 的命令发送频率。
- `log_remote`：默认 `false`。为 `true` 时打印 decoded sticks 和 button names。
- `remote_deadzone`：默认 `0.08`。仅用于 remote decode logging。

## Policy Run

默认 `enable_motion=false`，即使 policy 加载成功也不会发布运动命令：

```bash
ros2 run a2_lowlevel a2_policy_deploy
```

显式启用 motion，并使用静态 command provider：

```bash
ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=true \
  -p cmd_vx:=0.0 \
  -p cmd_vy:=0.0 \
  -p cmd_yaw:=0.0
```

显式启用 motion，并使用 A2 R3 remote command provider：

```bash
ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=true \
  -p command_source:=remote \
  -p max_remote_vx:=0.4 \
  -p max_remote_vy:=0.25 \
  -p max_remote_yaw:=0.6 \
  -p remote_deadzone:=0.08
```

参数：

- `policy_path`：默认 `policy/A2_policy/policy.pt`。
- `policy_json_path`：默认 `policy/A2_policy/policy.json`。
- `enable_motion`：默认 `false`。为 `false` 时不发布 motion command。
- `command_source`：默认 `static`，可选 `static` / `remote`。
- `cmd_vx` / `cmd_vy` / `cmd_yaw`：static command provider，默认全 `0.0`。
- `max_remote_vx` / `max_remote_vy` / `max_remote_yaw`：remote stick 映射上限，默认 `0.4`、`0.25`、`0.6`。
- `remote_deadzone`：remote stick deadzone，默认 `0.08`。
- `state_timeout_ms`：默认 `200`，沿用 `A2LowLevelInterface` fresh-state 判断。
- `lowstate_topic`：默认 `lowstate`，ROS2 graph 通常显示为 `/lowstate`。
- `lowcmd_topic`：默认 `lowcmd`，ROS2 graph 通常显示为 `/lowcmd`。

Remote mapping：

```text
cmd_vx  =  ly * max_remote_vx
cmd_vy  = -lx * max_remote_vy
cmd_yaw = -rx * max_remote_yaw
```

`ry` 不参与 command，只在 debug log 中保留。Remote 只作为 command provider 和 safety gate；不会直接写 `LowCmd`，policy output 仍然只经过 `A2LowLevelInterface::publish_joint_commands()`。

## Safety

真实硬件上使用 low-level command 前，必须确认 Unitree 内置运动控制服务 `ai_sports` / `ai_sport` 已关闭，否则底层服务可能不响应或发生控制冲突。当前不会自动 stand-up，也不会自动关闭 `ai_sport` / `ai_sports`。

`stand_test` 只是接口 smoke，不包含起身流程、姿态保护、limit check 或 emergency stop；首次运行应离地、限功率、有人值守，并准备硬件急停。

`a2_policy_deploy` 的 publish refusal 条件：

- `enable_motion=false`：node 仍监听 fresh `LowState`、更新 command provider、计算 observation 并 warm history，但在 `computeAction()` / `publish_joint_commands()` 前拒绝 motion publish
- missing/stale configured lowstate topic（默认 `/lowstate`）
- `LowState`、observation 或 action 出现 `NaN` / `Inf`
- `command_source` 非 `static` / `remote`
- `command_source=remote` 时 remote stick decode invalid
- observation/action dimension 不符合 contract
- history 尚未 warm 到 `32` fresh frames

这些条件下 node 不发布 motion command。

Remote safety gate：

- `L2` 必须按住才允许 nonzero policy command；未按住时 active command 强制为 `[0,0,0]`。
- `Select` 或 `L2+B` 触发 local stop：清空 policy/history/action runtime，调用 `publish_zero()`，并要求 release 后重新 fresh-state + history warmup。这个 zero LowCmd 是显式 safe stop command，即使 `enable_motion=false` 也可能发布；不会发布 policy motion command。

## CRC

A2 CRC 在 `a2_crc` 中独立实现，不复用 Go2W CRC。实现按手册 `LowCmd_` layout 构造显式 raw struct：

`mode_pr, mode_machine, 35 raw MotorCmd, reserve[4], crc`

然后对 `crc` 之前的 32-bit words 计算 CRC，避免依赖 ROS2 generated message struct 的内存 layout。

## Policy Boundary

policy output 只映射成 `std::array<A2JointCommand, 12>` 并调用 `publish_joint_commands()`。policy 侧不直接写 `unitree_hg::msg::LowCmd`，也不绕过 fresh-state guard、mode routing 和 A2 CRC。

## Deploy Machine Validation Checklist

当前 A2 R3 remote layout 来自 Unitree SDK2 sample，仍需在部署机或实机 configured lowstate topic（默认 `/lowstate`）的 `wireless_remote[40]` 上验证：

- `a2_lowlevel_smoke --ros-args -p log_remote:=true` 能随 stick/button 变化打印对应 `lx/rx/ry/ly` 和 button names。
- `L2` gate release 时 `a2_policy_deploy command_source=remote` 只给 policy command `[0,0,0]`。
- `L2` held 时 mapping 符合预期方向：`ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
- `Select` 与 `L2+B` 会触发 local stop、发布 zero LowCmd，并要求 history 重新 warm 到 `32` fresh frames。
- 验证过程中继续保持离地或限功率、关闭 `ai_sport` / `ai_sports`、准备 hardware emergency stop。
