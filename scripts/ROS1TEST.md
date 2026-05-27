# Aliengo ROS1 实机测试步骤（TX2 Relay 版）

**全程确保 Aliengo 处于低层模式且保护架已装好。**

> 当前架构：Docker (x86_64) → TX2 relay (ARM64, SDK v3.0.0) → 运动控制器。
> 详见 [`ros1/README.md`](../ros1/README.md)。

---

## 架构总览

```
ROG Docker (x86_64)             TX2 (ARM64)               Controller
aliengo_deploy node        →    aliengo_relay         →    192.168.123.10
(policy + gate + standup)  ←    (SDK v3.0.0 转发)     ←    (电机控制器)
  192.168.123.100                192.168.123.12:9000         :8007

协议:
  Docker ──UDP 730B LowCmd──→ TX2:9000
  Docker ←─UDP 891B LowState─← TX2:9000
```

---

### 第 0 步：确认网络连通（已通过 ✓）

```bash
# Docker 容器内
ping 192.168.123.12        # TX2 (relay)
ping 192.168.123.10        # Controller (可选，仅验证网络)
```

---

### 第 1 步：TX2 上启动 relay

```bash
ssh unitree@192.168.123.12
cd /home/unitree/unitree_legged_sdk
sudo env LD_LIBRARY_PATH=lib ./aliengo_relay
```

预期输出：
```
[aliengo_relay] Listening for external LowCmd on port 9000
[aliengo_relay] SDK connected to controller.
```

> ⚠️ 必须用 `sudo` 运行（SDK 需要 root 权限访问 UDP 端口）。
> 如果修改了 relay 源码，需要重新编译：
> ```bash
> g++ -I include -L lib -O2 -o aliengo_relay aliengo_relay.cpp -lunitree_legged_sdk -lpthread -llcm
> ```

---

### 第 2 步：Python 诊断验证（可选，推荐首次测试时做）

```bash
# Docker 容器内，确保 aliengo_deploy 没运行
# 使用直连控制器模式验证 IMU + 电机数据
python3 /work/AliengoSim2Real/scripts/aliengo_v3_udp_diag.py
```

已确认：
- [x] IMU quaternion ≈ `[0.98, -0.005, 0.005, 0.20]`
- [x] 12 电机 `mode=10`, `q` 值合理（hip ≈ ±0.17, thigh ≈ 1.2-1.4, calf ≈ -2.8）
- [x] motor struct = 32 字节（Auto-detected）
- [x] footForce 非零
- [ ] **关节映射手动确认**（逐腿摇晃 + 对比输出）

> ⚠️ 关节映射验证：之前摇晃 RR 小腿时观察到 FR_calf 变化，可能说明映射不完全对齐。
> 需要在 Python 诊断工具运行时，**逐条腿逐个关节**手动摇晃并确认对应 `motor[i]` 的 `q` 值变化。
> 如果发现映射错误，修改 [`aliengo_constants.h`](../ros1/include/aliengo_deploy/aliengo_constants.h:49) 中的 `kJointMap[12]`。

---

### 第 3 步：验证遥控器数据

在 Python 诊断工具运行时，按遥控器 A/B/Start 等按键，观察 `Remote keys=0x????` 是否变化。

```
# 预期
按 A:    keys 的 bit8 变 1  (0x0100)
按 B:    keys 的 bit9 变 1  (0x0200)
按 Start: keys 的 bit2 变 1
推摇杆:  lx/ly/rx 值变化
```

> 遥控器字节在 wire 格式 offset 206 处（非结构体尾部）。详见 [`aliengo_hardware_notes.md`](aliengo_hardware_notes.md)。

---

### 第 4 步：编译 aliengo_deploy

```bash
# Docker 容器内
cd /root/catkin_ws
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

> 如果是从零搭建 catkin workspace，见 [`ros1ENV.MD`](ros1ENV.MD) 步骤 4。
> 首次编译需要先创建软链接：
> ```bash
> mkdir -p /root/catkin_ws/src
> ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
> ln -sf /work/projects/AliengoSim2Real/ros1 /root/catkin_ws/src/aliengo_deploy
> ```

---

### 第 5 步：启动 aliengo_deploy（先不按 A 键）

**先停掉 Python 诊断工具**（如果在运行的话）。

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

roslaunch aliengo_deploy aliengo_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/ \
    gate_preset:=v2_robust \
    robot_ip:=192.168.123.12 \
    robot_port:=9000 \
    force_log_csv:=/tmp/force_estimator_log.csv
```

预期日志：
```
[UdpTransport] Started. Target: 192.168.123.12:9000, Local port: 8091
[UdpTransport] First state received (891 bytes).
Aliengo deploy node started. Control freq: 50 Hz. Mode: DIRECT_UDP.
[Manager] Policy 0 Initialized Successfully.
CSV force estimator log opened: /tmp/force_estimator_log.csv
```

> 注意：现在连接的是 TX2 relay (192.168.123.12:9000)，不是控制器 (192.168.123.10:8007)。
> relay 的 LowState 是 891 字节（struct 格式），不是 820 字节（wire 格式）。

---

### 第 6 步：在第二个终端查看 force_estimator

```bash
docker exec -it noetic-gpu bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

rostopic echo /force_estimator
```

应该看到 `pred_est` 数据流（策略加载后即开始推理，虽然还没使能电机命令）。

如果启动时传了 `force_log_csv:=/tmp/force_estimator_log.csv`，可以在容器里确认 CSV：

```bash
docker exec noetic-gpu head -5 /tmp/force_estimator_log.csv
docker cp noetic-gpu:/tmp/force_estimator_log.csv ./
```

---

### 第 7 步：⚠️ 保护架上按 A — 启动 Stand-Up

**确保：**
1. 机器人悬挂在保护架上，四足离地（首次测试）或四足触地但保护架兜住
2. 遥控器已开机
3. 旁边有人准备按 B 停止

在遥控器上 **按 A**，观察：

1. **Stage 1** (0~3s)：四条腿协调向默认站姿插值，日志显示 `front_alpha` 和 `rear_alpha`
2. **Stage 2** (3~6s)：继续协调插值到默认站姿，后腿略提前、前腿略滞后
3. **Wait** (6s 后)：保持默认站姿，打印 `Stand-up reached default pose. Press A again to enable policy.`
4. **Second A**：再次按 A 后策略接管，打印 `Stand-up confirmed by second A. Policy ENABLED.`

如果有异常（单腿乱甩、关节角度跳变、NaN），立即 **按 B** 停止。

> 站立参数在 [`aliengo_constants.h`](../ros1/include/aliengo_deploy/aliengo_constants.h:174)：
> - `kStandUpStage1Steps = 150` (3.0s)
> - `kStandUpStage2Steps = 150` (3.0s)
> - `kStandUpRearAlphaLead = 0.10`
> - `kStandUpFrontAlphaLag = 0.04`
> - `kStandUpKpStart = 3.0` (起始 Kp，逐步增大)

---

### 第 8 步：试按 B 受控停止

按遥控器 B 键，观察：
- 打印 `Controlled STOP requested`
- 腿缓慢回到站立 → 趴下姿态（约 2.8 秒）
- 最终保持趴下

---

### 第 9 步：试按 L2+B 紧急制动

如果策略行为异常需要立即停止：
- 同时按 L2+B
- 打印 `EMERGENCY DAMPING STOP`
- 所有电机切换为阻尼模式（Kp=0, Kd=3.0），机器人会自然坍塌

---

### 第 10 步：试推摇杆发速度指令

重新按 A 完成站立后：
- 推左摇杆前 → vx 增加，步态应变化
- 推左摇杆左右 → wz 转向
- 推右摇杆左右 → vy 侧移

> 速度指令缩放在 [`aliengo_constants.h`](../ros1/include/aliengo_deploy/aliengo_constants.h:187)：
> `kPadScaleVx = 1.0`, `kPadScaleVy = 1.0`, `kPadScaleWz = 1.0`

---

## 验证清单

| # | 项目 | 状态 |
|---|------|------|
| 1 | 网络 ping 通 (TX2 + Controller) | ✅ |
| 2 | TX2 relay 正常启动 | ✅ |
| 3 | Python diag 收到有效数据 (IMU/电机/footForce) | ✅ |
| 4 | 遥控器按键/摇杆确认 (A=0x0100, B=0x0200) | ✅ |
| 5 | **逐腿关节映射确认** | ⬜ 关键！|
| 6 | aliengo_deploy 编译成功 | ✅ |
| 7 | aliengo_deploy 只读启动（连接 relay，不按 A） | ✅ |
| 8 | force_estimator 有数据 | ✅ |
| 9 | 保护架上按 A → stand-up 正常 | ✅ |
| 10 | 按 B 受控停止 | ⬜ |
| 11 | 推摇杆测试 | ⬜ |
| 12 | 脱离保护架地面行走 | ⬜ |

---

## 常见问题

### relay 没收到数据
- 检查 Docker 容器 `--network host` 是否启用
- 检查 `robot_ip` 和 `robot_port` 参数是否正确
- TX2 上 `netstat -ulnp | grep 9000` 确认 relay 在监听

### 站立时翻倒
- 检查 PD 增益是否过大（先用 `kKp` 的 50%）
- 检查关节映射是否正确（最可能的原因）
- 确保保护架可靠

### 策略输出 NaN
- 检查 obs 是否有 NaN（IMU 断线、电机通信丢失）
- 检查 policy.pt 是否与当前 obs 维度匹配（46×32=1472）

### 遥控器按键无反应
- 确认 relay 正在转发完整的 LowState（包含 wirelessRemote 字段）
- 直连模式下遥控器数据在 wire offset 206，relay 模式下在 struct offset 843
