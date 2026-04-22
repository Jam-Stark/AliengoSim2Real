# Aliengo ROS1 RL Policy Deployment

ROS1 (Noetic) catkin 包，用于在 Unitree Aliengo 四足机器人上部署 RL 运动策略。

## 概述

此包将训练好的 TorchScript 策略（12 维关节位置控制器）通过 ROS1 话题
与 Unitree 官方 `unitree_ros_to_real` 的 `ros_udp` 桥接节点通信，
实现 50 Hz 低层闭环控制。

## 架构

```
ros_udp bridge  <-- UDP -->  Aliengo Robot (192.168.123.10)
    │
    ├── pub: low_state (unitree_legged_msgs/LowState)
    └── sub: low_cmd   (unitree_legged_msgs/LowCmd)
    │
aliengo_deploy node
    ├── 订阅 low_state → 提取 IMU / 关节观测
    ├── 运行 policy inference (50 Hz)
    └── 发布 low_cmd → 关节位置 PD 指令
```

## 文件结构

```
ros1/
├── CMakeLists.txt                     # catkin 构建配置
├── package.xml                        # catkin 包声明
├── README.md                          # 本文件
├── docker/
│   └── Dockerfile                     # Noetic + CUDA + LibTorch 镜像
├── launch/
│   └── aliengo_deploy.launch          # 同时启动 ros_udp + deploy 节点
├── include/aliengo_deploy/
│   ├── aliengo_constants.h            # 关节映射/默认位姿/PD增益/obs规格
│   ├── aliengo_deploy_node.h          # 主节点头文件
│   ├── gait_clock.h                   # 步态相位时钟
│   └── wireless_remote_decoder.h      # 遥控器字节解码器
├── src/
│   ├── aliengo_deploy_main.cpp        # main 入口
│   ├── aliengo_deploy_node.cpp        # 主节点实现
│   ├── gait_clock.cpp                 # (header-only placeholder)
│   └── wireless_remote_decoder.cpp    # 遥控器解码实现
└── scripts/
    └── start_aliengo_deploy.sh        # 一键启动脚本
```

## 依赖

- ROS1 Noetic
- `unitree_legged_msgs` (来自 `unitree_ros_to_real`)
- `unitree_legged_real` (来自 `unitree_ros_to_real`，提供 `ros_udp`)
- `unitree_legged_sdk` (Aliengo 固件对应版本的 SDK)
- LibTorch ≥ 2.0 (或 ONNX Runtime，通过 `USE_ONNX=ON` 切换)
- OpenCV, Eigen3, jsoncpp, libudev

## 环境搭建 (远程机 Docker)

### 方案 A（推荐）: 在已有 noetic-gpu 容器内追加 LibTorch

如果你已按 `scripts/ros1ENV.MD` 成功构建了 `noetic-gpu:2026-04`，
只需在容器内安装 LibTorch：

```bash
# 启动已有容器
docker run -it --rm \
  --name noetic-gpu \
  --gpus all --network host --ipc host \
  -v $HOME/Downloads/WorkSpace:/work \
  --device /dev/input:/dev/input \
  noetic-gpu:2026-04

# 容器内安装 LibTorch
cd /opt
wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip
unzip libtorch.zip && rm libtorch.zip
echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
source /root/.bashrc
```

### 方案 B: 从头构建完整镜像

```bash
cd ros1/docker
docker build -t aliengo-deploy:latest .
docker run -it --rm --name aliengo-deploy \
  --gpus all --network host --ipc host \
  -v $HOME/Downloads/WorkSpace:/work \
  --device /dev/input:/dev/input \
  aliengo-deploy:latest
```

### 3. 初始化 unitree_legged_sdk

```bash
cd /work/projects/unitree_ros_to_real
git submodule update --init --recursive
```

### 4. 构建 catkin workspace

```bash
source /opt/ros/noetic/setup.bash
mkdir -p /root/catkin_ws/src

# 软链接所有包
ln -s /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -s /work/projects/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -s /work/projects/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -s /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

# 编译
cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

## 使用

### 方式 1: Launch 文件 (推荐)

```bash
source /root/catkin_ws/devel/setup.bash
roslaunch aliengo_deploy aliengo_deploy.launch \
    policy_path:=/path/to/your/policy/directory/
```

### 方式 2: 启动脚本

```bash
chmod +x ros1/scripts/start_aliengo_deploy.sh
./ros1/scripts/start_aliengo_deploy.sh /path/to/your/policy/directory/
```

### 方式 3: 分步启动

终端 1 — 启动 ros_udp bridge:
```bash
roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
```

终端 2 — 启动策略节点:
```bash
rosrun aliengo_deploy aliengo_deploy policy_path=/path/to/policy/
```

## 遥控器操作

| 按键 | 功能 |
|------|------|
| **A** | 使能策略（开始运动） |
| **B** | 受控停止（站立→卧倒） |
| **L2+B** | 紧急制动（阻尼模式） |
| **Start** | 清零速度指令 |
| **Select** | 重置策略状态 |
| 左摇杆前后 | vx（前进/后退） |
| 左摇杆左右 | wz（转向） |
| 右摇杆左右 | vy（侧移） |

## Policy 规格

| 项目 | 值 |
|------|------|
| 输入 | 1472 = 46 × 32 history |
| 输出 | 12 delta joint pos + 6 pred_est |
| 动作 | q_target = q_default + 0.25 × action |
| 频率 | 50 Hz |
| 格式 | TorchScript (.pt) |

观测向量 (单帧 46 维):
- projected_gravity[x,y] (2)
- base_ang_vel × 0.25 (3)
- joint_pos - default (12)
- joint_vel × 0.05 (12)
- last_action_raw (12)
- gait_clock [sin,cos] (2)
- commands × [2.0, 2.0, 0.25] (3)

## 安全注意事项

1. **首次测试必须使用保护架悬挂机器人**
2. 上电后默认处于零力矩模式，需按 A 键才会启用策略
3. 任何时候按 B 键可安全趴下，L2+B 可紧急制动
4. PD 增益值来自 MuJoCo 仿真导出，实机可能需要调整
5. 确保以太网连接稳定 (192.168.123.x 网段)
