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
    ├── setup_and_build.sh             # 完整搭建步骤参考
    └── start_aliengo_deploy.sh        # 一键启动脚本
```

## 依赖

- ROS1 Noetic
- `unitree_legged_msgs` (来自 `unitree_ros_to_real`)
- `unitree_legged_real` (来自 `unitree_ros_to_real`，提供 `ros_udp`)
- `unitree_legged_sdk` (Aliengo 固件对应版本的 SDK)
- LibTorch ≥ 2.0 CPU 版（或 ONNX Runtime，通过 `USE_ONNX=ON` 切换）
- OpenCV, Eigen3, jsoncpp, libudev

## 环境搭建 (远程机 Docker)

### 前提

- 已按 `scripts/ros1ENV.MD` 构建了 `noetic-gpu:2026-04` Docker 镜像
- 远程机宿主机 `~/Downloads/WorkSpace/` 目录下有以下仓库：

```
~/Downloads/WorkSpace/
├── AliengoSim2Real/        ← 本仓库（含 ros1/ 代码和 policy/）
│   ├── ros1/
│   ├── policy/aliengo/     ← 放 policy.pt
│   └── utils/              ← 推理核心 (cpp_manager_env 等)
└── unitree_ros_to_real/    ← Unitree 官方 ROS1 包
    ├── unitree_legged_msgs/
    ├── unitree_legged_real/
    └── unitree_legged_sdk/
```

Docker 启动时 `-v $HOME/Downloads/WorkSpace:/work` 映射后，
容器内路径对应为 `/work/AliengoSim2Real/` 和 `/work/unitree_ros_to_real/`。

### 步骤 1: 宿主机 — 启动容器

```bash
docker run -it --name noetic-gpu --gpus all --network host \
  -v $HOME/Downloads/WorkSpace:/work \
  noetic-gpu:2026-04
```

> 不加 `--rm`，这样容器内安装的 LibTorch 不会丢失。后续用 `docker start -i noetic-gpu` 重新进入。

### 步骤 2: 容器内 — 安装 LibTorch CPU 版（只需做一次）

```bash
cd /opt
wget -q "https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcpu.zip" -O libtorch.zip
unzip -q libtorch.zip && rm libtorch.zip
echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
source /root/.bashrc
```

### 步骤 3: 宿主机 — 初始化 unitree_legged_sdk

在**宿主机**终端执行（避免容器内 git safe.directory 问题）：

```bash
cd ~/Downloads/WorkSpace/unitree_ros_to_real
git submodule update --init --recursive
# 如果失败：
#   rm -rf unitree_legged_sdk
#   git clone https://github.com/unitreerobotics/unitree_legged_sdk.git
```

### 步骤 4: 容器内 — 构建 catkin workspace

```bash
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"

mkdir -p /root/catkin_ws/src

# 软链接所有包（注意路径：/work/ 下直接是仓库名，没有 projects/ 子目录）
ln -sf /work/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -sf /work/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

# 编译
cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release

# Source 工作区（每次新开 shell 都需要）
source devel/setup.bash
```

### 步骤 5: 容器内 — 验证

```bash
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

rospack find aliengo_deploy        # 应输出: /root/catkin_ws/src/aliengo_deploy
rospack find unitree_legged_real   # 应输出: /root/catkin_ws/src/unitree_legged_real
```

## 放置 Policy 文件

将 `policy.pt` 放到远程机宿主机上：

```
~/Downloads/WorkSpace/AliengoSim2Real/policy/aliengo/policy.pt
```

容器内对应路径为：`/work/AliengoSim2Real/policy/aliengo/policy.pt`

## 使用

> 以下所有命令均在 **Docker 容器内**执行。

### 方式 1: Launch 文件 (推荐)

```bash
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

roslaunch aliengo_deploy aliengo_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/
```

### 方式 2: 分步启动

终端 1 — 启动 ros_udp bridge:
```bash
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
```

终端 2 — 启动策略节点:
```bash
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
rosrun aliengo_deploy aliengo_deploy \
    policy_path=/work/AliengoSim2Real/policy/aliengo/
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
6. 每次新开容器 shell，都必须执行 `source /opt/ros/noetic/setup.bash && source /root/catkin_ws/devel/setup.bash`
