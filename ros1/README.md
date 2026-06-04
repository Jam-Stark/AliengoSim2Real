# Aliengo ROS1 RL Policy Deployment

在 Unitree Aliengo (v3.0.0 固件) 上部署 RL 运动策略的完整框架。

## 架构

```
macOS (开发机)          ROG Docker (x86_64)         TX2 (ARM64)            Controller
编写代码 → rsync →     aliengo_deploy node    →    aliengo_relay     →    192.168.123.10
                       (policy inference       ←    (SDK v3.0.0       ←    (电机控制器)
                        + standing/walking gate      UDP 转发)
                        + brake gate
                        + force estimator
                        + CSV logging
                        + stand-up 前段)

通信协议:
  Docker ──UDP 730B LowCmd──→ TX2:9000 (relay)
  Docker ←─UDP 891B LowState─← TX2:9000 (relay)
  TX2    ──SDK UDP──→ 192.168.123.10:8007
  TX2    ←─SDK UDP──← 192.168.123.10:8007
```

**为什么需要 TX2 relay？** Aliengo v3.0.0 的运动控制器只接受来自板载 PC (TX2/MiniPC) 的电机命令，外部 PC 的 UDP 命令会被忽略（但状态数据可以收到）。relay 在 TX2 上用原生 SDK 转发命令。

## 文件结构

```
ros1/
├── CMakeLists.txt
├── package.xml
├── README.md                              # 本文件
├── docker/Dockerfile                      # Noetic + LibTorch CPU Docker 镜像
├── tx2_relay/
│   └── aliengo_relay.cpp                  # TX2 上运行的 SDK UDP 中继
├── include/aliengo_deploy/
│   ├── aliengo_constants.h                # 关节映射/默认位姿/PD增益/obs规格
│   ├── aliengo_deploy_node.h              # 主节点
│   ├── aliengo_udp_transport.h            # 直接 UDP 通信层
│   ├── brake_command_gate.h               # 刹车保护门
│   ├── force_mode_switcher.h              # standing/walking gate (v2/v2_robust)
│   ├── gait_clock.h                       # 步态相位时钟
│   └── wireless_remote_decoder.h          # 遥控器字节解码
├── src/
│   ├── aliengo_deploy_main.cpp            # main 入口
│   ├── aliengo_deploy_node.cpp            # 主节点实现
│   ├── aliengo_udp_transport.cpp          # UDP 收发实现
│   ├── gait_clock.cpp
│   ├── wireless_remote_decoder.cpp
│   └── test/                              # 无实机接口级测试
│       ├── fake_low_state_publisher.cpp
│       └── low_cmd_monitor.cpp
├── launch/
│   ├── aliengo_deploy.launch              # 实机部署
│   └── test_deploy.launch                 # 无实机测试
└── scripts/                               # ros1-local scripts, not legacy/stale top-level scripts/
    ├── deploy/                             # canonical Aliengo deploy docs
    ├── setup_and_build.sh
    └── start_aliengo_deploy.sh
```

## 快速开始

### 前提

1. ROG 笔记本有 Docker + `noetic-gpu:2026-04` 镜像
2. Aliengo TX2 可 SSH 访问 (unitree@192.168.123.12)
3. TX2 上已编译 `aliengo_relay`（见下方"TX2 Relay 搭建"）
4. `policy.pt` 放在 `policy/aliengo/` 目录

### 步骤 1: TX2 上启动 relay

```bash
ssh unitree@192.168.123.12
cd /home/unitree/unitree_legged_sdk
sudo env LD_LIBRARY_PATH=lib ./aliengo_relay
```

### 步骤 2: Docker 中启动部署节点

```bash
docker start -i noetic-gpu
# 容器内:
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

roslaunch aliengo_deploy aliengo_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/ \
    gate_preset:=v2_robust \
    force_log_csv:=/tmp/force_estimator_log.csv
```

### 步骤 3: 遥控器操作

| 按键 | 功能 |
|------|------|
| **A** | 第一次启动 stand-up，默认站姿后第二次接入策略 |
| **B** | 受控停止（站立→趴下） |
| **L2+B** | 紧急阻尼制动 |
| **Start** | 清零速度指令 |
| **Select** | 重置策略状态 |
| 左摇杆 | vx (前后) + wz (左右转向) |
| 右摇杆 | vy (侧移) |

## Stand-Up 前段

第一次按 A 后不会直接启用策略，而是先完成 stand-up 插值：

1. **Stage 1** (0~3s): 四条腿协调向默认站姿插值，日志显示 `front_alpha/rear_alpha`
2. **Stage 2** (3~6s): 继续协调插值到默认站姿，后腿略提前、前腿略滞后以抑制后仰
3. **Wait**: 持续保持默认站姿，不自动接入策略
4. **Second A**: 再按 A 后 warm-start obs history → 策略接管

参数在 `aliengo_constants.h` 中可调。

## TX2 Relay 搭建

### 首次编译

```bash
# 把 relay 源码传到 TX2
scp ros1/tx2_relay/aliengo_relay.cpp unitree@192.168.123.12:/home/unitree/unitree_legged_sdk/

# SSH 到 TX2 编译
ssh unitree@192.168.123.12
cd /home/unitree/unitree_legged_sdk
g++ -I include -L lib -O2 -o aliengo_relay aliengo_relay.cpp \
    -lunitree_legged_sdk -lpthread -llcm
```

### 运行

```bash
sudo env LD_LIBRARY_PATH=/home/unitree/unitree_legged_sdk/lib ./aliengo_relay
```

## Launch 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `policy_path` | ...policy/aliengo/ | TorchScript 模型目录 |
| `gate_preset` | v2 | standing/walking gate 预设 (v2 / v2_robust) |
| `robot_ip` | 192.168.123.12 | TX2 relay IP |
| `robot_port` | 9000 | TX2 relay 端口 |
| `use_direct_udp` | true | 直接 UDP 模式 (false=ROS topic 模式，用于 fake test) |
| `inference_device` | cpu | cpu / cuda |
| `gait_frequency` | 2.0 | gait clock 频率 (Hz) |
| `force_log_csv` | 空 | 非空时保存 pred_est/gate/brake CSV，例如 `/tmp/force_estimator_log.csv` |

## Docker 环境搭建

见 `scripts/deploy/ros1ENV.MD`。`scripts/deploy/` 是本 `ros1/` 目录内的 canonical Aliengo deploy docs location；legacy/stale 顶层 `scripts/` references 应改指向 `ros1/scripts/deploy/`。

核心要点：
- 使用 `noetic-gpu:2026-04` Docker 镜像
- 容器内安装 LibTorch CPU 版到 `/opt/libtorch`
- catkin workspace 通过软链接组装

## 关键参数调整

### PD 增益

在 `aliengo_constants.h` 中：
```cpp
constexpr float kKp[12] = { hip×4, thigh×4, calf×4 };
constexpr float kKd[12] = { hip×4, thigh×4, calf×4 };
```
当前从 MuJoCo 导出，实机上可能需要降低 30-50%。

### 关节映射

如果实机关节顺序与标准 Aliengo URDF 不一致（FR_hip=0, FR_thigh=1, ...），需修改 `kJointMap[12]`。

### Stand-Up 参数

```cpp
kStandUpStage1Steps / kStandUpStage2Steps
kStandUpRearAlphaLead / kStandUpFrontAlphaLag
kStandUpKpStart / kStandUpKdStart
```

## 安全注意事项

1. **首次部署务必使用保护架**
2. 第一次按 A 后有 6 秒站立插值，随后等待第二次 A 才接入策略
3. 随时按 B 安全趴下，L2+B 紧急制动
4. 关节映射必须通过手动验证确认
5. PD 增益从低开始，逐步增大
