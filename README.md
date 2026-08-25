# AliengoSim2Real

AliengoSim2Real 是一个 general robot policy deployment framework。repo 同时维护 MuJoCo experiment area、shared C++ policy runtime、policy assets、ROS1 Aliengo deployment、ROS2 Go2W deployment、ROS2 A2 low-level adapter、A2 PC2 上的 PiPER ROS 2 bridge，以及 A2 + PiPER Stage2 dual-policy 外部策略 runtime。

## Repository Map

- `policy/`: TorchScript / ONNX policy assets and metadata.
- `utils/cpp_manager_env/`: shared `ManagerBasedEnv`、`PolicySpec`、observation/action runtime。
- `mujoco/`: MuJoCo C++ / Python experiment and sim tooling。
- `robot/`: robot description assets。
- `ros1/`: Aliengo ROS1 `aliengo_deploy` package，包含 TX2 relay、direct UDP、stand-up/gate/brake logic。
- `ros2/src/`: Go2W ROS2 `go2w_vtm` package，包含 `go2w_real_deploy`、`go2w_stand_example`、`deep_camera`。
- `ros2/A2/`: A2 ROS2 `a2_lowlevel` package，包含已在同一台 A2 部署的 locomotion policy、LowState/LowCmd adapter、remote handover 与操作脚本。
- `ros2/Piper/`: A2 PC2 独占 PiPER USB-CAN 的 ROS 2 semantic bridge、remote manipulation adapter 与 deployment/validation tooling。
- `deploy/a2_piper_stage2/`: 真实 LMP Stage2 dog+arm bundle、复用 `A2LowLevelInterface` 的 C++ direct runtime、offline parity/mock、Docker 与分阶段现场 Gate。
- `ros2/A2_Guide/`: A2 SDK/reference docs，memory 中只引用该目录。

## Memory Routing

开始实现、调试、review 或文档更新前，先读 `MEMORY.md`。常用 route：

- Global overview/runtime: `memory/MEMORY.md`
- ROS1 Aliengo: `ros1/memory/MEMORY.md`
- ROS2 Go2W: `ros2/src/memory/MEMORY.md`
- ROS2 A2: `ros2/A2/MEMORY.md`
- ROS2 PiPER bridge: `ros2/Piper/MEMORY.md`
- A2 + PiPER Stage2: `deploy/a2_piper_stage2/MEMORY.md`

## Main Deployment Entrypoints

### ROS1 Aliengo

```bash
cd ros1
# 详见 ros1/README.md 和 ros1/scripts/deploy/ros1ENV.MD
```

Aliengo route 使用 ROS Noetic / catkin / LibTorch，并通过 `TX2 relay` 转发 low-level command 到 Aliengo controller。

### ROS2 Go2W

```bash
cd ros2
source /opt/ros/humble/setup.bash
source ~/unitree_ros2/setup_local.sh
colcon build --packages-select go2w_vtm --cmake-args -DUSE_ONNX=ON -DBUILD_TESTING=OFF
source install/setup.bash
ros2 run go2w_vtm go2w_real_deploy
```

Go2W route 默认使用 ONNX Runtime，支持多 policy switching、local USB gamepad 和 Unitree wireless controller。

### ROS2 A2

```bash
cd ros2
source /opt/ros/humble/setup.bash
source ~/unitree_ros2/setup_local.sh
colcon build --packages-select a2_lowlevel --cmake-args -DBUILD_TESTING=OFF
source install/setup.bash
ros2 run a2_lowlevel a2_lowlevel_smoke
```

A2 route 已包含同一台 A2 成功部署的 `a2_policy_deploy`：它在同一 C++ 进程内读取 `A2LowLevelInterface` snapshot、完成 training/raw motor mapping 并只通过 `publish_joint_commands()` 进入 `LowCmd`。实机起身、remote handover、stop 和 MotionSwitcher 步骤统一从 `ros2/A2/scripts/A2_REAL_DEPLOY_RUNBOOK.md` 执行，不从本 README 直接启动 live。

### ROS2 A2 PC2 → PiPER

```bash
cd ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select piper_bridge
source install/setup.bash
ros2 run piper_bridge piper_smoke_test
```

PiPER route 由 A2 PC2 本地独占 USB-CAN，并通过 `/piper/*` ROS 2 interface 与笔记本交换 6 关节状态和目标。PC2 Docker、SocketCAN、watchdog 和现有 manipulation task 的 remote runner 见 `ros2/Piper/README.md`。

### A2 + PiPER Stage2 dual-policy

```bash
cd deploy/a2_piper_stage2
./scripts/configure_policy_host.sh \
  --iface enp130s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.mock.yaml"
./scripts/stage2_gate.sh init --operator <name>
./scripts/stage2_gate.sh offline
./scripts/stage2_gate.sh next
```

该 route 使用用户提供的真实 `54×30 -> 12` dog actor 与 `20×30 -> 8` arm actor。正式实机路径不新增 A2 semantic bridge，而是与 main 成功案例一样在 C++ 进程内复用 `A2LowLevelInterface`；PiPER 仍经 PC2 semantic bridge，PC2 仍是 CAN 唯一 owner。实机 live 仍必须完成 site limits/watchdog/stop 与逐 Gate 现场 receipts。

## MuJoCo / C++ Dependencies

MuJoCo experiment area 仍在 `mujoco/`。常见依赖：

```bash
git clone https://github.com/google-deepmind/mujoco.git
cd mujoco
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

如果使用 MuJoCo release path，可在对应 `CMakeLists.txt` 中设置：

```CMake
set(MUJOCO_PATH "your mujoco path")
include_directories(${MUJOCO_PATH}/include)
link_directories(${MUJOCO_PATH}/build/bin)
set(MUJOCO_LIB ${MUJOCO_PATH}/build/lib/libmujoco.so)
```

LibTorch CPU example:

```bash
wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.8.0%2Bcpu.zip
export Torch_DIR=/your_path/libtorch
```

Gamepad dependencies:

```bash
sudo apt-get install libudev-dev joystick
```

## Safety

Real robot deployment must start with robot-specific safety checks: joint order, PD gains, policy action scaling, controller service conflicts, protected stand/smoke flow, and hardware emergency stop. Do not reuse one robot's low-level command assumptions for another robot without validating the message type, CRC/layout, motor order and built-in motion service state.
