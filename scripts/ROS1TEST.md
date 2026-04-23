# Aliengo ROS1 实机测试步骤（直接 UDP 版）

**全程确保 Aliengo 处于低层模式且保护架已装好。**

> 当前使用直接 UDP 通信（`AliengoUdpTransport`），不需要 `ros_udp` bridge。

---

### 第 0 步：确认网络连通（已通过 ✓）

```bash
# Docker 容器内
ping 192.168.123.10
# 应有回复
```

---

### 第 1 步：用 Python 诊断工具验证数据（已通过 ✓）

```bash
# 确保 ros_udp 和 aliengo_deploy 都没运行（它们也绑定 8091）
python3 /work/AliengoSim2Real/scripts/aliengo_v3_udp_diag.py
```

已确认：
- [x] IMU quaternion ≈ `[0.98, -0.005, 0.005, 0.20]`
- [x] 12 电机 `mode=10`, `q` 值合理（hip ≈ ±0.17, thigh ≈ 1.2-1.4, calf ≈ -2.8）
- [x] motor struct = 32 字节（Auto-detected）
- [x] footForce 非零
- [ ] 关节映射手动确认（逐腿摇晃 + 对比输出）

> ⚠️ 关节映射验证：你之前摇晃 RR 小腿时观察到 FR_calf 变化，这可能说明映射不完全对齐。
> 需要在 Python 诊断工具运行时，**逐条腿逐个关节**手动摇晃并确认对应 `motor[i]` 的 `q` 值变化。

---

### 第 2 步：验证遥控器数据

在 Python 诊断工具运行时，按遥控器 A/B/Start 等按键，观察 `Remote keys=0x????` 是否变化。

```
# 预期
按 A:    keys 的 bit8 变 1
按 B:    keys 的 bit9 变 1
按 Start: keys 的 bit2 变 1
推摇杆:  lx/ly/rx 值变化
```

---

### 第 3 步：编译 aliengo_deploy（直接 UDP 版）

```bash
# Docker 容器内
cd /root/catkin_ws
rm -rf build devel
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

确认 `aliengo_deploy` 编译成功。

---

### 第 4 步：启动 aliengo_deploy 做只读测试

**先不按 A 键**，只看它能不能收到状态并加载模型。

```bash
# 先停掉 Python 诊断工具（释放 8091 端口）

export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

roslaunch aliengo_deploy aliengo_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/ \
    gate_preset:=v2_robust
```

预期日志：
```
[UdpTransport] Started. Target: 192.168.123.10:8007, Local port: 8091
[UdpTransport] First state received (820 bytes).
Aliengo deploy node started. Control freq: 50 Hz. Mode: DIRECT_UDP.
[Manager] Policy 0 Initialized Successfully.
```

---

### 第 5 步：在第二个终端查看 force_estimator

```bash
docker exec -it noetic-gpu bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

rostopic echo /force_estimator
```

应该看到 `pred_est` 数据流。

---

### 第 6 步：⚠️ 保护架上按 A 使能策略

**确保：**
1. 机器人悬挂在保护架上，四足离地
2. 遥控器已开机
3. 旁边有人准备按 B 停止

在遥控器上**按 A**，观察：
- 部署节点打印 `Policy ENABLED by remote A button.`
- 机器人腿开始动（应接近默认站姿附近的小幅运动）
- `/force_estimator` 数据在变化

如果有异常（腿乱动、关节角度跳变、NaN），立即**按 B** 停止。

---

### 第 7 步：试按 B 受控停止

按遥控器 B 键，观察：
- 打印 `Controlled STOP requested`
- 腿缓慢回到站立 → 趴下姿态
- 最终保持趴下

---

### 第 8 步：试推摇杆发速度指令

重新按 A 使能后：
- 推左摇杆前 → vx 增加，机器人腿动作应变化
- 推右摇杆左右 → vy 变化

---

### 总结：验证顺序

1. [x] 网络 ping 通
2. [x] Python diag 收到有效数据
3. [x] IMU 合理
4. [ ] **逐腿关节映射确认**（关键！）
5. [ ] 遥控器按键/摇杆确认
6. [ ] aliengo_deploy 编译成功
7. [ ] aliengo_deploy 只读启动（不按 A）
8. [ ] force_estimator 有数据
9. [ ] 保护架上按 A 使能
10. [ ] 按 B 受控停止
11. [ ] 推摇杆测试
