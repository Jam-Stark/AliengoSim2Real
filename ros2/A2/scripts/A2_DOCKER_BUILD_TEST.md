# A2 Docker Build/Test Guide

本文档面向部署机 operator，在连接 A2 real hardware 之前，于部署机
`/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real` 执行 A2 Docker build
和 offline smoke。目标是验证 Docker image、ROS2 Humble / CycloneDDS、
`unitree_hg` messages、`a2_lowlevel`、`a2_policy_deploy` 的基础 readiness。

## 0. Scope and Safety Boundary

本流程默认 **不连接真实 A2**，也不做 low-level motion。

禁止项：

- 不连接 real A2 DDS/control network。
- 不运行任何需要真实低层控制闭环的 motion test。
- 不设置 `enable_motion:=true`。
- 不运行 `stand_test`。
- 不运行 `publish_zero`，除非进入本文档明确标注的 optional zero-command path，且只在 fake/no hardware 场景使用。

真实 A2 DDS/control validation 只能在部署机 + robot network 上做，不能用 Mac Docker
Desktop networking 结论替代。连接实机前还必须确认 `ai_sport` / `ai_sports` 已关闭、
机器人处于安全支撑或限功率状态，并准备 hardware emergency stop。

## 1. Host Prerequisites and Paths

部署机当前 expected path：

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
pwd
```

Ideal result：

```text
/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
```

确认 Docker 可用：

```bash
docker --version
docker info >/dev/null
echo "docker-ok"
```

Ideal result：

- `docker --version` 输出 Docker version。
- `docker info` 无 error。
- 终端打印 `docker-ok`。

默认 image / platform：

```bash
export A2_DOCKER_IMAGE="${A2_DOCKER_IMAGE:-a2-humble-deploy:2026-06-05}"
export A2_DOCKER_PLATFORM="${A2_DOCKER_PLATFORM:-linux/amd64}"
printf 'image=%s\nplatform=%s\n' "$A2_DOCKER_IMAGE" "$A2_DOCKER_PLATFORM"
```

Ideal result：

```text
image=a2-humble-deploy:2026-06-05
platform=linux/amd64
```

`linux/amd64` 是当前 formal deploy target。只在 debug 特殊架构时覆盖
`A2_DOCKER_PLATFORM`。

## 2. Preflight Before Building Image

未连接 A2 前，`enp131s0` 没有 `192.168.123.x` 是可接受的。这个阶段只确认 host
state，不会修改 network。

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
bash ros2/A2/docker/preflight.sh --iface enp131s0
```

Ideal result：

- `Docker` section 显示 Docker version。
- 如果 image 尚未 build，显示 `image: MISSING a2-humble-deploy:2026-06-05`，这是 build 前正常结果。
- `A2 Network Interface` 能看到 `candidate iface: enp131s0`。
- 未连接 A2 时，`A2 low-level subnet: MISSING 192.168.123.x on enp131s0` 可接受。
- `Workspace Mount` 显示 `AliengoSim2Real mount source: FOUND`。
- 不应出现脚本修改 host network 的行为。

不要在未连接 A2 时把 `192.168.124.x` 当作本链路的 low-level DDS subnet；当前
official DDS reference names `rt/lowstate` / `rt/lowcmd` 使用 `192.168.123.x`。
本仓库 ROS2 backend 默认 visible topics 是 `/lowstate` / `/lowcmd`；旧 `/rt/...`
不再是默认 ROS2 topic。

## 3. Build Docker Image

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
bash ros2/A2/docker/build_image.sh
```

Ideal result：

- Docker build 完成，没有 `ERROR` 或 failed layer。
- 最后一行类似：

```text
[a2-docker] built image: a2-humble-deploy:2026-06-05 (linux/amd64)
```

确认 image 存在：

```bash
docker image inspect a2-humble-deploy:2026-06-05 >/dev/null
echo "image-found"
```

Ideal result：

```text
image-found
```

如果旧 container 内 `/opt/a2/build_a2_workspace.sh` 曾因
`AMENT_TRACE_SETUP_FILES: unbound variable` 失败，先重新 build image；该 helper 在
Docker build 时复制进 image，旧 image 不会自动获得修复。

## 4. Preflight After Image

image build 后增加 `--container-check`，验证 container 内 ROS2 / Unitree message
readiness。仍然不连接 A2，不 ping robot。

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
bash ros2/A2/docker/preflight.sh --iface enp131s0 --container-check
```

Ideal result：

- `image: FOUND a2-humble-deploy:2026-06-05`。
- `ROS_DISTRO=humble`。
- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`。
- `ros2 pkg prefix unitree_hg`、`unitree_go`、`unitree_api` 都能返回路径。
- `Unitree ROS2 message interfaces: OK`。
- 如果还没 build workspace，`A2 workspace install: MISSING; run /opt/a2/build_a2_workspace.sh` 是正常结果。
- 未连接 A2 时 `192.168.123.x` missing 仍可接受。

如要在连接 A2 后检查 subnet / ping，必须另行手动配置 NIC：

```bash
sudo ip link set enp131s0 up
sudo ip addr flush dev enp131s0
sudo ip addr add 192.168.123.99/24 dev enp131s0
bash ros2/A2/docker/preflight.sh --iface enp131s0 --ping
```

这一步不是本文 offline scope 的必需项；连接 real A2 前不要继续 motion path。

## 5. Enter Container Offline

offline container 使用 loopback interface，避免误连 real A2 network：

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
A2_NET_IFACE=lo bash ros2/A2/docker/run_container.sh bash
```

Ideal result：

- 进入 container shell。
- entrypoint 打印：

```text
[a2-entrypoint] RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
[a2-entrypoint] A2_NET_IFACE=lo
[a2-entrypoint] Using loopback. Set A2_NET_IFACE=enp131s0 for real A2 hardware.
```

在 container 内确认 environment：

```bash
echo "ROS_DISTRO=${ROS_DISTRO}"
echo "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "Torch_DIR=${Torch_DIR}"
printf '%s\n' "$CYCLONEDDS_URI"
ls -d /opt/libtorch /opt/unitree/unitree_ros2 /opt/unitree/unitree_sdk2
pwd
```

Ideal result：

- `ROS_DISTRO=humble`。
- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`。
- `Torch_DIR=/opt/libtorch/share/cmake/Torch`。
- `CYCLONEDDS_URI` 中包含 `NetworkInterface name="lo"`。
- `/opt/libtorch`、`/opt/unitree/unitree_ros2`、`/opt/unitree/unitree_sdk2` 都存在。
- `pwd` 是 `/work/projects/AliengoSim2Real/ros2`。

## 6. ROS2 Unitree Interface Checks

在 container 内执行：

```bash
ros2 pkg prefix unitree_hg
ros2 interface show unitree_hg/msg/LowCmd
ros2 interface show unitree_hg/msg/LowState
ros2 interface show unitree_hg/msg/MotorCmd
```

Ideal result：

- `ros2 pkg prefix unitree_hg` 返回 `/opt/unitree/.../install/unitree_hg` 类似路径。
- `LowCmd` 包含：

```text
uint8 mode_pr
uint8 mode_machine
MotorCmd[35] motor_cmd
uint32[4] reserve
uint32 crc
```

- `LowState` 包含：

```text
uint32[2] version
uint8 mode_pr
uint8 mode_machine
uint32 tick
IMUState imu_state
MotorState[35] motor_state
uint8[40] wireless_remote
uint32[4] reserve
uint32 crc
```

- `MotorCmd` 包含：

```text
uint8 mode
float32 q
float32 dq
float32 tau
float32 kp
float32 kd
uint32 reserve
```

如果这些字段不一致，停止后续 build，把完整 `ros2 interface show ...` 输出发回。

## 7. Build A2 Lowlevel and Policy

在 container 内 build low-level adapter：

```bash
/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 pkg prefix a2_lowlevel
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_lowlevel_smoke
ls -l /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_lowlevel_smoke
```

Ideal result：

- `colcon` 输出包含 `Finished <<< a2_lowlevel`。
- `ros2 pkg prefix a2_lowlevel` 返回 `/work/projects/AliengoSim2Real/ros2/install/a2_lowlevel`。
- `test -x` 无输出且 exit code 为 `0`。
- `ls` 能看到 executable `a2_lowlevel_smoke`。

在 container 内 build optional policy target：

```bash
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ls -l /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel
```

Ideal result：

- `colcon` 输出包含 `Finished <<< a2_lowlevel`。
- `ls` 能看到 `a2_lowlevel_smoke` 和 `a2_policy_deploy`。
- build log 中不应出现 Torch/jsoncpp missing error；`Torch_DIR` 应使用 `/opt/libtorch/share/cmake/Torch`。

## 8. Offline Virtual Smoke Tests

以下 smoke 都在 offline container 内执行，默认 `A2_NET_IFACE=lo`。如果开多个终端，
每个终端都用同一个 offline container，或用新的 container 但保持 `A2_NET_IFACE=lo`。

### 8.1 No-Lowstate Smoke

无 fake publisher、无 robot 时：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 5 ros2 run a2_lowlevel a2_lowlevel_smoke
echo "exit=$?"
```

Ideal result：

- log 包含 `A2 low-level interface ready: pub /lowcmd, sub /lowstate`。
- log 包含 `listen-only mode: no LowCmd will be published.`。
- 每秒打印 `Waiting for /lowstate...`。
- 进程因 `timeout 5` 退出，exit code 通常是 `124`。
- 不应 crash，不应发布 motion command。

如果 shell 因 `timeout` nonzero 中断，可用：

```bash
timeout 5 ros2 run a2_lowlevel a2_lowlevel_smoke || echo "timeout-or-nonzero=$?"
```

### 8.2 Fake LowState Publisher + Smoke

Terminal A 启动 fake `/lowstate`：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 topic pub -r 20 /lowstate unitree_hg/msg/LowState '{}'
```

Ideal result：

- publisher 打印 `publisher: beginning loop` 或持续 publish。
- 如果 ROS2 CLI 因 generated message shape 拒绝 `{}`，这是 message-default CLI limitation；
  本阶段跳过 fake lowstate，等待后续增加 dedicated fake publisher，不要改用真实 A2。

Terminal B 运行 smoke：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 8 ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p log_remote:=true || echo "timeout-or-nonzero=$?"
```

Ideal result：

- log 从 `Waiting for /lowstate...` 变为包含 `tick=0 mode_pr=0 mode_machine=0`。
- `joint_q=[0.000, ...]`、`joint_dq=[0.000, ...]`。
- `remote_sticks=[lx=0.000, rx=0.000, ry=0.000, ly=0.000] buttons=none`，或 remote default 相关 zero log。
- 不 crash，不发布 motion command。

### 8.3 Policy Load / Listen-Only with Fake LowState

保持 Terminal A fake `/lowstate` 运行。Terminal B 执行：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 12 ros2 run a2_lowlevel a2_policy_deploy --ros-args \
  -p enable_motion:=false \
  -p command_source:=static \
  -p cmd_vx:=0.0 \
  -p cmd_vy:=0.0 \
  -p cmd_yaw:=0.0 || echo "timeout-or-nonzero=$?"
```

Ideal result：

- log 包含 `Validated A2 policy contract: action_dim=12, per_frame_obs=46, history=32`。
- log 包含 `A2 policy deploy ready: ... enable_motion=false, command_source=static`。
- log 包含 `A2 policy history warming`，随后可能出现 `A2 policy history warm: 32 frames`。
- log 包含 `A2 policy publish refused because enable_motion=false.`。
- 不应出现 policy load failure、observation invalid、action dim invalid、segmentation fault。
- 不发布 motion command。

### 8.4 Verify No LowCmd Under enable_motion=false

Terminal A 保持 fake `/lowstate`。Terminal B 运行 policy listen-only。Terminal C 执行：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 5 ros2 topic echo /lowcmd --once
echo "exit=$?"
```

Ideal result：

- 5 秒内没有 `/lowcmd` message。
- `timeout` exit code 通常是 `124`。
- 这说明 `enable_motion=false` 下没有 lowcmd publish。

如果 shell 因 nonzero 中断，可用：

```bash
timeout 5 ros2 topic echo /lowcmd --once || echo "expected-timeout=$?"
```

### 8.5 Optional Zero-Command Path, Fake/No Hardware Only

本步骤是 non-motion zero-command path，只用于确认 `publish_zero()` 能 publish 一帧
zero `LowCmd`。**连接真实 A2 时不要执行**，除非 safety checklist 已完成且 operator 明确需要
zero command。

Terminal A：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 5 ros2 topic echo /lowcmd --once
```

Terminal B：

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
timeout 3 ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p publish_zero:=true || echo "timeout-or-nonzero=$?"
```

Ideal result：

- Terminal B log 明确 warning：`publish_zero will publish zero/stop LowCmd frames`。
- Terminal A 收到一条 `unitree_hg/msg/LowCmd`。
- 该 message 中 `mode_pr=0`、`mode_machine=0`，`motor_cmd` 的 `q/dq/tau/kp/kd` 为 `0.0`，`crc` 有计算值。
- 没有 `stand_test`，没有 nonzero joint target。

## 9. Final Acceptance Criteria Before Connecting Real A2

连接真实 A2 前，以下全部应为 pass：

- 部署机 path 是 `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`。
- Docker 可用，image `a2-humble-deploy:2026-06-05` build 成功，platform 是 `linux/amd64`。
- `preflight.sh --container-check` 显示 `ROS_DISTRO=humble`、`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`、`Unitree ROS2 message interfaces: OK`。
- offline container 使用 `A2_NET_IFACE=lo` 时，`CYCLONEDDS_URI` 指向 `lo`。
- `unitree_hg/msg/LowCmd`、`LowState`、`MotorCmd` 字段与本文档一致。
- `/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release` 完成，`a2_lowlevel_smoke` 可运行。
- `/opt/a2/build_a2_workspace.sh --policy --cmake-release` 完成，`a2_policy_deploy` 可运行。
- no-lowstate smoke 能等待 `/lowstate` 且不 crash。
- fake lowstate smoke 能接收 default zero state；如 `{}` CLI limitation 触发，已记录并跳过，不误连硬件。
- policy listen-only 在 `enable_motion=false` 下能 load policy / validate contract / warm history，且 `/lowcmd` 无消息。
- optional zero-command path 未在 real hardware 上执行。
- 实机 safety checklist 已准备：关闭 `ai_sport` / `ai_sports`、离地或限功率、hardware emergency stop。

## 10. Remains Unvalidated Until Real A2 Connection

以下项目必须等部署机连接 robot network 后验证，offline Docker 不代表 pass：

- `enp131s0` 真实 NIC 是否正确接入 A2 network。
- host 是否正确配置 `192.168.123.99/24`，A2 `192.168.123.x` 是否可达。
- robot 是否持续发布真实 ROS2 visible `/lowstate`。
- `/lowcmd` CRC 在真实 Unitree A2 low-level path 中的行为。
- `wireless_remote[40]` 的真实 byte layout、stick direction、button state。
- `a2_lowlevel_smoke -p log_remote:=true` 的真实 remote decode。
- `ai_sport` / `ai_sports` 关闭流程。
- policy timing、control stability、fresh-state behavior、low-level command effect。
- 任何 `enable_motion:=true` path。

## 11. Failure Logs to Send Back

如失败，请发回以下信息，避免只描述现象：

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
git rev-parse --short HEAD
docker --version
docker image inspect a2-humble-deploy:2026-06-05 --format '{{.Id}} {{.Architecture}} {{.Os}}' 2>&1
bash ros2/A2/docker/preflight.sh --iface enp131s0 --container-check 2>&1 | tee /tmp/a2_preflight_container_check.log
```

在 container 内：

```bash
echo "ROS_DISTRO=${ROS_DISTRO}"
echo "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "Torch_DIR=${Torch_DIR}"
printf '%s\n' "$CYCLONEDDS_URI"
ros2 pkg prefix unitree_hg
ros2 interface show unitree_hg/msg/LowCmd
ros2 interface show unitree_hg/msg/LowState
ros2 interface show unitree_hg/msg/MotorCmd
/opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release 2>&1 | tee /tmp/a2_lowlevel_build.log
/opt/a2/build_a2_workspace.sh --policy --cmake-release 2>&1 | tee /tmp/a2_policy_build.log
timeout 8 ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args -p log_remote:=true 2>&1 | tee /tmp/a2_smoke.log
timeout 12 ros2 run a2_lowlevel a2_policy_deploy --ros-args -p enable_motion:=false 2>&1 | tee /tmp/a2_policy_listen_only.log
```

需要回传的文件：

- `/tmp/a2_preflight_container_check.log`
- `/tmp/a2_lowlevel_build.log`
- `/tmp/a2_policy_build.log`
- `/tmp/a2_smoke.log`
- `/tmp/a2_policy_listen_only.log`
- 如果 fake lowstate `{}` 失败，回传完整 `ros2 topic pub` error。
