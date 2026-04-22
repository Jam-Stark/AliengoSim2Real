# Aliengo ROS1 静态测试指南

无实机情况下，使用 fake 状态发布器 + 策略节点 + 输出监视器进行接口级测试。

## 前提

- Docker 容器 `noetic-gpu` 已启动
- catkin workspace 已编译通过（含 `aliengo_deploy`、`fake_low_state_publisher`、`low_cmd_monitor`）
- `policy.pt` 已放置在 `/work/AliengoSim2Real/policy/aliengo/`

## 重新编译（如有代码变更）

在容器内执行：

```bash
cd /root/catkin_ws
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

## 运行测试

需要 **2 个容器终端**。第二个终端通过 `docker exec -it noetic-gpu bash` 进入。

### 终端 1 — 策略节点 + 输出监视器

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

roslaunch aliengo_deploy test_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/
```

### 终端 2 — fake 状态发布器（带键盘控制）

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

rosrun aliengo_deploy fake_low_state_publisher
```

## 终端 2 键盘操作

| 按键 | 动作 |
|------|------|
| `a` | 使能策略（模拟遥控 A 键） |
| `b` | 受控停止（模拟遥控 B 键） |
| `e` | 紧急制动（模拟 L2+B） |
| `w`/`x` | 增加/减少 vx 命令 |
| `d`/`c` | 增加/减少 wz 命令 |
| `s` | 清零速度指令（模拟 Start） |
| `r` | 重置策略（模拟 Select） |
| `0` | 摇杆归零 |
| `q` | 退出 |

## 预期验证结果

1. **终端 1 启动后**：显示 `Waiting for low_state...` 和 `Waiting for messages on /low_cmd...`
2. **终端 2 启动后**：终端 1 出现 `First low_state received.`，监视器显示 12 电机数据（Kp=0，哨兵值模式）
3. **按 `a`**：终端 1 打印 `Policy ENABLED`，监视器中 12 电机 Kp 变非零、q 值为正常关节角度
4. **按 `w`**：给前进速度指令，动作输出应随之变化
5. **按 `b`**：终端 1 打印 `Controlled STOP`，监视器中 Kp=60/Kd=5 的缓慢趴下过程
6. **按 `e`**：终端 1 打印 `EMERGENCY DAMPING STOP`，监视器中 Kp=0/Kd=3
7. **全程**：监视器 NaN 计数保持为 0，无关节超限警告
