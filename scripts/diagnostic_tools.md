# 诊断工具使用指南

本文档介绍本仓库中用于 Aliengo 实机调试的各种诊断工具。

## 1. aliengo_v3_udp_diag.py — UDP 原始包诊断

**位置**: `scripts/aliengo_v3_udp_diag.py`
**运行环境**: Docker 容器内（或任何能到达 192.168.123.10 的机器）
**前提**: 没有其他程序占用 8091 端口（aliengo_deploy、ros_udp 都要先停）

### 用途

直接和 Aliengo 控制器 (192.168.123.10:8007) 做 UDP 通信，解析 v3.0.0 LowState 包，显示：
- IMU 四元数、角速度、RPY
- 12 个电机的 mode、q、dq
- 遥控器按键和摇杆值
- 自动检测电机结构体线上大小

### 运行

```bash
python3 /work/AliengoSim2Real/scripts/aliengo_v3_udp_diag.py
```

### 输出示例

```
Auto-detected motor struct: 32 bytes (score=12/12)
Idx  Name         mode  q(rad)     dq         tauEst
0    FR_hip       10    -0.1641    0.0000     0.000
1    FR_thigh     10    1.2484     -0.0000    0.000
...
Remote keys=0x0100 lx=0.000 ly=0.500 rx=0.000
```

### 关键功能

- **关节映射验证**: 手动推某条腿的关节，观察哪个 motor[i] 变化
- **遥控器验证**: 按 A/B/Start，看 keys 是否变化
- **自动检测 MotorState 线上大小**: 从 26-43 字节逐个尝试

### 注意

此工具直连控制器（不经过 relay），所以：
- 能读状态 ✓
- 不能发有效命令 ✗（控制器不接受外部 PC 的命令）
- 只用于诊断，不用于控制

---

## 2. fake_low_state_publisher — 无实机测试

**位置**: `ros1/src/test/fake_low_state_publisher.cpp`
**运行环境**: Docker 容器内
**编译后**: `/root/catkin_ws/devel/lib/aliengo_deploy/fake_low_state_publisher`

### 用途

模拟 Aliengo 机器人，以 500Hz 发布合成 LowState ROS 消息，用于 **无实机** 时测试 aliengo_deploy 节点。

### 运行

```bash
# 终端 1: 启动 deploy 节点（use_direct_udp=false）
roslaunch aliengo_deploy test_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/

# 终端 2: 启动 fake publisher (带键盘控制)
rosrun aliengo_deploy fake_low_state_publisher
```

### 键盘控制

| 按键 | 动作 |
|------|------|
| `a` | 模拟遥控 A（使能策略） |
| `b` | 模拟遥控 B（停止） |
| `e` | 模拟 L2+B（急停） |
| `w`/`x` | 增/减 vx |
| `d`/`c` | 增/减 wz |
| `s` | 清零命令 |
| `r` | 重置策略 |
| `q` | 退出 |

---

## 3. low_cmd_monitor — 电机命令监视器

**位置**: `ros1/src/test/low_cmd_monitor.cpp`
**运行环境**: Docker 容器内（与 aliengo_deploy 同时运行）

### 用途

订阅 `/low_cmd` ROS 话题，实时显示 12 个电机的命令值，检测异常。

### 功能

- 显示每个电机的 q、Kp、Kd、dq、tau、mode
- 检测 NaN / Inf
- 检测关节位置超限
- 统计发布频率
- 对称性检查（FR vs FL）

### 运行

```bash
rosrun aliengo_deploy low_cmd_monitor
```

> **注意**: 只在 ROS topic 模式（`use_direct_udp=false`）下有效。直接 UDP 模式下 low_cmd 不发布到 ROS。

---

## 4. 原始 UDP hex dump — 手动包分析

### 基础收发测试

```python
python3 -c "
import socket, struct
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2.0)
s.bind(('0.0.0.0', 8091))
s.sendto(b'\x00'*730, ('192.168.123.10', 8007))
data, addr = s.recvfrom(4096)
print(f'Received {len(data)} bytes from {addr}')
nonzero = sum(1 for b in data if b != 0)
print(f'Non-zero bytes: {nonzero} / {len(data)}')
s.close()
"
```

### 按键差异 diff

用于定位 wirelessRemote 的字节偏移：

```bash
# 不按任何键
python3 -c "..." > /tmp/dump_nokey.txt

# 按住 A 键
python3 -c "..." > /tmp/dump_a_key.txt

diff /tmp/dump_nokey.txt /tmp/dump_a_key.txt
```

### sizeof 验证（在 TX2 上）

```bash
cat << 'EOF' > /tmp/print_sizeof.cpp
#include <stdio.h>
#include "unitree_legged_sdk/comm.h"
int main() {
    printf("sizeof(LowCmd) = %zu\n", sizeof(UNITREE_LEGGED_SDK::LowCmd));
    printf("sizeof(LowState) = %zu\n", sizeof(UNITREE_LEGGED_SDK::LowState));
    printf("sizeof(MotorCmd) = %zu\n", sizeof(UNITREE_LEGGED_SDK::MotorCmd));
    printf("sizeof(MotorState) = %zu\n", sizeof(UNITREE_LEGGED_SDK::MotorState));
    return 0;
}
EOF
cd /home/unitree/unitree_legged_sdk
g++ -I include -o /tmp/print_sizeof /tmp/print_sizeof.cpp && /tmp/print_sizeof
```

---

## 5. TX2 SDK example_position — 电机最小验证

在 TX2 上直接运行 SDK 自带的位置控制示例：

```bash
ssh unitree@192.168.123.12
cd /home/unitree/unitree_legged_sdk/build
sudo env LD_LIBRARY_PATH=../lib ./example_position
```

按 Enter 后 FR 腿会做正弦运动。用于确认：
- SDK 与控制器通信正常
- 电机响应命令
- 关节方向正确

---

## 6. relay 诊断日志

TX2 relay 运行时每 5 秒打印状态：

```
[Status] IMU quat_w=0.9998  motor[0].q=-0.7365  ext_connected=1
```

关键指标：
- `ext_connected=1`: Docker 已经连上 relay
- `quat_w ≈ 1.0`: IMU 正常
- `motor[0].q`: FR_hip 当前角度

### 添加命令诊断

修改 `aliengo_relay.cpp` 中 relay thread 的接收处添加 print：

```cpp
printf("[Relay] Received cmd: motor[0] mode=%d q=%.3f Kp=%.1f\n",
       ((LowCmd*)cmd_buf)->motorCmd[0].mode,
       ((LowCmd*)cmd_buf)->motorCmd[0].q,
       ((LowCmd*)cmd_buf)->motorCmd[0].Kp);
```

---

## 诊断决策树

```
问题: 机器人不响应命令
│
├─ relay 显示 ext_connected=0?
│   └─ Docker 的包没到 TX2 → 检查 robot_ip / robot_port / 网络
│
├─ relay 显示 ext_connected=1 但电机不动?
│   ├─ motor[0].q 在 relay status 中有微小变化? → Kp 太低
│   └─ motor[0].q 完全不变? → LowCmd 格式问题
│
├─ diag.py 能读到电机数据但全零?
│   └─ Aliengo 可能在 HIGH-LEVEL 模式 → 需要切换到 LOW-LEVEL
│
├─ diag.py 能读到正常电机数据?
│   └─ udp 通路 OK → 问题在 relay 或 deploy 节点逻辑
│
└─ diag.py 超时无回包?
    └─ 网络不通 → 检查 IP / 子网 / Docker --network host
```
