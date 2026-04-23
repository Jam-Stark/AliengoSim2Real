连上 TX2 后，按以下顺序做实机验证。**全程确保 Aliengo 处于低层模式且保护架已装好。**

---

### 第 0 步：确认网络连通

在你连接 TX2 的终端上：

```bash
# 确认 TX2 的 IP
ifconfig
# 应该看到 192.168.123.x 段的 IP

# ping 运动控制器
ping 192.168.123.10
# 应该有回复
```

如果你的外部 PC（Docker 宿主机）也连着 Aliengo 的以太网：

```bash
# 从宿主机 ping 控制器
ping 192.168.123.10
```

---

### 第 1 步：启动 ros_udp bridge

在 Docker 容器内（确保 `--network host`）：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

# 只启动 bridge，不启动 deploy 节点
roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
```

看到 `ros_udp` 启动后，**不要急着发命令**。先在另一个终端验证状态。

---

### 第 2 步：验证 low_state 能收到

开第二个容器终端（`docker exec -it noetic-gpu bash`）：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

# 列出话题
rostopic list
# 应该看到 /low_state 和 /low_cmd

# 查看 low_state 发布频率
rostopic hz /low_state
# ros_udp 会在收到 low_cmd 后才发布 low_state
# 如果还没有人发 low_cmd，可能看不到

# 先手动发一个空 low_cmd 触发 bridge
rostopic pub -1 /low_cmd unitree_legged_msgs/LowCmd "{}"
# 然后再看
rostopic hz /low_state
```

---

### 第 3 步：查看 IMU 和关节数据

```bash
# 查看完整 low_state（会刷屏，按 Ctrl+C 停）
rostopic echo /low_state --noarr -n 1

# 单独看 IMU 四元数
rostopic echo /low_state/imu/quaternion -n 3

# 单独看关节位置（12 个电机）
rostopic echo /low_state/motorState -n 1 | head -60
```

**此时你应该验证的关键数据：**

| 检查项                         | 预期                              |
| ------------------------------ | --------------------------------- |
| `imu.quaternion`             | 接近 `[1, 0, 0, 0]`（机身水平） |
| `imu.gyroscope`              | 接近 `[0, 0, 0]`（静止）        |
| `motorState[0].q` (FR_hip)   | 约 `-0.1`                       |
| `motorState[1].q` (FR_thigh) | 约 `0.5`                        |
| `motorState[2].q` (FR_calf)  | 约 `-1.0`                       |
| `motorState[3].q` (FL_hip)   | 约 `0.1`                        |
| `footForce[0..3]`            | 有非零值（如果站着）              |

---

### 第 4 步：验证关节映射

这是**最重要的一步**。需要确认 SDK 的 motorState 索引和你的 policy 训练时的关节定义是否匹配。

```bash
# 用 Python 看更清楚（容器内可能需要装 rospy）
# 或者直接用 rostopic echo 逐个看

# 看 motor 0 (应该是 FR_hip)
rostopic echo /low_state/motorState[0]/q -n 3

# 看 motor 3 (应该是 FL_hip)
rostopic echo /low_state/motorState[3]/q -n 3
```

**手动触碰验证法：**

1. 让 Aliengo 处于阻尼模式（L2+B）
2. 手动轻轻推动某条腿的某个关节（如 FR 的小腿）
3. 观察 `motorState[2].q`（FR_calf）是否变化
4. 逐条腿逐个关节确认映射

这样你就能知道：

- `motorState[0]` 是不是真的 FR_hip
- `motorState[3]` 是不是真的 FL_hip
- 等等

如果映射和 [`aliengo_constants.h`](ros1/include/aliengo_deploy/aliengo_constants.h) 中的 [`kJointMap`](ros1/include/aliengo_deploy/aliengo_constants.h:42) 一致，就说明关节定义正确。

---

### 第 5 步：验证遥控器数据

```bash
# 看 wirelessRemote 原始字节
rostopic echo /low_state/wirelessRemote -n 5
```

然后按遥控器上的 A、B 等按键，观察 `wirelessRemote` 字节数组的变化。
参考 [`joystick.h`](/Users/caobaoquan/Downloads/python/projects/unitree_legged_sdk/include/unitree_legged_sdk/joystick.h:32)：

- bytes 2-3：按键 bitfield
- bytes 4-7：左摇杆 X
- bytes 20-23：左摇杆 Y

---

### 第 6 步：用 state_sub 示例验证

`unitree_ros_to_real` 自带一个状态打印工具：

```bash
rosrun unitree_legged_real state_sub
```

它会持续打印 low_state 摘要。

---

### 第 7 步：用 example_position 做最小电机测试（⚠️ 保护架必须装好）

这是 Unitree 官方的位置控制示例，会让 **FR 腿** 做正弦运动：

```bash
# 确保机器人悬挂在保护架上！
rosrun unitree_legged_real ros_example_position
```

按 Enter 后 FR 腿会开始动。观察：

- 是不是 FR 腿在动（不是其他腿）
- 运动是否平滑
- 力矩是否正常

如果这一步通过，说明 `ros_udp bridge + 电机位置控制` 整条链路是对的。

---

### 第 8 步：启动 aliengo_deploy 做只读测试

**不按 A 键**，先只看它能不能正常收状态和加载模型：

```bash
# 终端 1: ros_udp 已在运行

# 终端 2:
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

rosrun aliengo_deploy aliengo_deploy \
    policy_path=/work/AliengoSim2Real/policy/aliengo/ \
    gate_preset=v2_robust

# 终端 3: 看 force_estimator 输出
rostopic echo /force_estimator
```

你应该看到：

- `First low_state received.`
- `/force_estimator` 有数据（如果 pred_est 正常）
- low_cmd monitor（如果开了）显示 PosStopF/VelStopF 哨兵值（因为还没按 A）

---

### 总结：验证顺序

1. 网络 ping ✓
2. ros_udp 启动 ✓
3. low_state 能收到 ✓
4. IMU 数据合理 ✓
5. 关节映射手动确认 ✓
6. 遥控器字节解码正确 ✓
7. example_position 单腿运动 ✓
8. aliengo_deploy 只读启动 ✓
9. **最后才按 A 使能策略**（确保以上全部通过）
