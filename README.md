# AliengoSim2Real

AliengoSim2Real 是一个 general robot policy deployment framework。repo 同时维护 MuJoCo experiment area、shared C++ policy runtime、policy assets、ROS1 Aliengo deployment、ROS2 Go2W deployment，以及 ROS2 A2 low-level adapter 起点。

## Repository Map

- `policy/`: TorchScript / ONNX policy assets and metadata.
- `utils/cpp_manager_env/`: shared `ManagerBasedEnv`、`PolicySpec`、observation/action runtime。
- `mujoco/`: MuJoCo C++ / Python experiment and sim tooling。
- `robot/`: robot description assets。
- `ros1/`: Aliengo ROS1 `aliengo_deploy` package，包含 TX2 relay、direct UDP、stand-up/gate/brake logic。
- `ros2/src/`: Go2W ROS2 `go2w_vtm` package，包含 `go2w_real_deploy`、`go2w_stand_example`、`deep_camera`。
- `ros2/A2/`: A2 ROS2 `a2_lowlevel` package，当前是 low-level adapter/smoke 起点，不接 policy。
- `ros2/A2_Guide/`: A2 SDK/reference docs，memory 中只引用该目录。

## Memory Routing

开始实现、调试、review 或文档更新前，先读 `MEMORY.md`。常用 route：

- Global overview/runtime: `memory/MEMORY.md`
- ROS1 Aliengo: `ros1/memory/MEMORY.md`
- ROS2 Go2W: `ros2/src/memory/MEMORY.md`
- ROS2 A2: `ros2/A2/MEMORY.md`

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

A2 route 当前只提供 low-level adapter/smoke，不包含起身流程、limit check、emergency stop 或 policy deployment。

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
