# Aliengo 实机硬件笔记

本文档记录当前这台 Aliengo 的硬件特性、SDK 版本、网络拓扑和已知问题。

## 基本信息

| 项目 | 值 |
|------|------|
| 型号 | Aliengo PRO |
| 固件代号 | Aliengo-TX2-spot-mode |
| SDK 版本 | v3.0.0 (2021-03-19) |
| 板载 PC | NVIDIA TX2 (ARM64, Ubuntu 18.04, ROS Melodic) |
| 控制器 IP | 192.168.123.10 |
| TX2 IP | 192.168.123.12 |
| TX2 SSH | `unitree@192.168.123.12` (密码: 123) |
| 关节数 | 12 (4 腿 × 3 关节) |
| 电机最大扭矩 | ~40 N·m |

## TX2 文件结构

```
/home/unitree/
├── unitree_legged_sdk/           ← v3.0.0 SDK (ARM64)
│   ├── build/                    ← 预编译 examples
│   │   └── example_position      ← 可直接运行验证电机
│   ├── include/unitree_legged_sdk/
│   │   ├── comm.h                ← LowState/LowCmd 结构体定义
│   │   ├── udp.h                 ← UDP 类接口
│   │   ├── joystick.h            ← 遥控器字节布局
│   │   └── aliengo_const.h       ← 关节限位
│   ├── lib/
│   │   └── libunitree_legged_sdk.so  ← ARM64 SDK 二进制
│   ├── aliengo_relay             ← 我们编译的 UDP relay
│   └── version.txt               ← "Aliengo-TX2-spot-mode: v3.0.0"
└── catkin_ws/src/
    └── slamrplidar/              ← SLAM 包 (不相关)
```

## SDK v3.0.0 特有行为

### 与 GitHub 最新版 SDK 的差异

| 项目 | v3.0.0 (TX2) | GitHub 最新 |
|------|-------------|-------------|
| UDP 构造函数 | `UDP(level)` 或 5 参数 | `UDP(port, sendLen, recvLen)` 3 参数 |
| 默认端口 | 8080 | 8091 |
| LowState 线上大小 | 820 字节 (压缩) | == sizeof |
| sizeof(LowState) | 891 | 不同的头 (head[2]) |
| sizeof(LowCmd) | 730 | 不同 |
| sizeof(MotorState) | 38 | 38 (但线上 32) |
| LowState 头 | levelFlag+commVersion+robotID+SN+bandWidth | head[2]+levelFlag+frameReserve+SN[2]+version[2]+bandWidth |
| LCM 依赖 | 有 | 无 |
| CRC | 始终为 0 | 可能非零 |

### 线上包格式（直连控制器时收到的 820 字节）

```
[0:10]    Header (10B): levelFlag+commVersion+robotID+SN+bandWidth
[10:63]   IMU (53B): quat[4]+gyro[3]+acc[3]+rpy[3]+temp
[63:703]  MotorState[20] (20×32B): 线上每个电机 32 字节 (不是 38)
[703:820] Tail (117B): footForce + footForceEst + tick + ... + 未知字段
WirelessRemote: 绝对偏移 206 (在电机区域中间，不在 tail)
```

### 通过 relay 收到的 891 字节（struct 格式）

```
[0:10]    Header (10B)
[10:63]   IMU (53B)
[63:823]  MotorState[20] (20×38B): 完整 struct 格式
[823:831] footForce[4] (8B)
[831:839] footForceEst[4] (8B)
[839:843] tick (4B)
[843:883] wirelessRemote[40] (40B)
[883:887] reserve (4B)
[887:891] crc (4B)
```

## 网络拓扑

```
外部 PC (192.168.123.20)
    |
    | 以太网 (192.168.123.x 子网)
    |
TX2 (192.168.123.12) ←→ 控制器 (192.168.123.10:8007)
    |
    | 内部直连
    |
MiniPC (可能 192.168.123.13/14)
```

**关键限制**: 控制器只接受来自板载 PC (TX2/MiniPC) 的电机命令。外部 PC 可以读状态但不能写命令。

## 遥控器

- 手柄通过专用数传模块连接（非 WiFi/蓝牙）
- 数据嵌在 LowState 的 `wirelessRemote[40]` 中
- 按键 bitfield 布局见 `joystick.h`
- 实测确认: A=0x0100, B=0x0200, Start=0x0004
- 摇杆: lx(@+4), rx(@+8), ly(@+20) 均为 float [-1, 1]

## 关节映射（待完全验证）

标准 Aliengo SDK 顺序：
```
motor[0] = FR_hip      motor[3] = FL_hip
motor[1] = FR_thigh    motor[4] = FL_thigh
motor[2] = FR_calf     motor[5] = FL_calf
motor[6] = RR_hip      motor[9] = RL_hip
motor[7] = RR_thigh    motor[10]= RL_thigh
motor[8] = RR_calf     motor[11]= RL_calf
```

## 关节限位（来自 aliengo_const.h）

| 关节 | 最小 (rad) | 最大 (rad) |
|------|-----------|-----------|
| Hip | -0.873 (-50°) | 1.047 (60°) |
| Thigh | -0.524 (-30°) | 3.927 (225°) |
| Calf | -2.775 (-159°) | -0.611 (-35°) |

## 已知问题

1. SDK v3.0.0 的 LowState 线上格式和 sizeof 不一致（820 vs 891）
2. 遥控器 wirelessRemote 在线上包中的偏移是 206，不在 tail 区域
3. 外部 PC 无法直接发送电机命令到控制器
4. TX2 没有 `tcpdump` 和 `strace`，调试需要自写工具
5. `example_position` 的 FR_hip 动作幅度很小 (Kp=5 不足以克服摩擦)
