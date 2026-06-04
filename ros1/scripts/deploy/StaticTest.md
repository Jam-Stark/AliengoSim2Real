# Aliengo ROS1 静态测试指南

无实机情况下，使用 fake 状态发布器 + 策略节点 + 输出监视器进行接口级测试。

## 前提

- Docker 容器 `noetic-gpu` 已启动
- catkin workspace 已编译通过（含 `aliengo_deploy`、`fake_low_state_publisher`、`low_cmd_monitor`）
- `policy.pt` 已放置在 `/work/AliengoSim2Real/policy/aliengo/`
- LibTorch CPU 已安装在 `/opt/libtorch`

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

> `test_deploy.launch` 使用 `use_direct_udp:=false`，通过 ROS topic 收发，不需要实机/relay。

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
| `a` | 第一次启动 stand-up，默认站姿后第二次接入策略（模拟遥控 A 键） |
| `b` | 受控停止（模拟遥控 B 键） |
| `e` | 紧急阻尼制动（模拟 L2+B） |
| `w`/`x` | 增加/减少 vx 命令 |
| `d`/`c` | 增加/减少 wz 命令 |
| `s` | 清零速度指令（模拟 Start） |
| `r` | 重置策略（模拟 Select） |
| `0` | 摇杆归零 |
| `q` | 退出 |

## 预期验证结果

1. **终端 1 启动后**：显示 `Waiting for low_state...` 和 `Waiting for messages on /low_cmd...`
2. **终端 2 启动后**：终端 1 出现 `First low_state received.`，监视器显示 12 电机数据（Kp=0，哨兵值模式）
3. **第一次按 `a`**：终端 1 打印 stand-up 阶段日志，默认站姿后提示 `Press A again to enable policy`
4. **第二次按 `a`**：终端 1 打印 `Policy ENABLED`，监视器中 12 电机 Kp 变非零、q 值为正常关节角度
5. **按 `w`**：给前进速度指令，动作输出应随之变化
6. **按 `b`**：终端 1 打印 `Controlled STOP`，监视器中 Kp=60/Kd=5 的缓慢趴下过程
7. **按 `e`**：终端 1 打印 `EMERGENCY DAMPING STOP`，监视器中 Kp=0/Kd=3
8. **全程**：监视器 NaN 计数保持为 0，无关节超限警告

## 可选：查看 force_estimator topic

在第三个终端（`docker exec`）中：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

rostopic echo /force_estimator
```

应看到 `geometry_msgs/WrenchStamped` 格式的 pred_est 数据流。

如果需要保存 CSV，启动 `test_deploy.launch` 时加：

```bash
force_log_csv:=/tmp/force_estimator_log.csv
```

## 与实机测试的区别

| 对比项 | 静态测试 | 实机测试 |
|--------|----------|----------|
| 通信方式 | ROS topic (`use_direct_udp=false`) | 直接 UDP via TX2 relay |
| 状态来源 | `fake_low_state_publisher` | TX2 relay 转发 |
| Stand-Up | 数值模拟（不驱动真实电机） | 实际 6 秒插值 + 第二次 A 接入策略 |
| 遥控器 | 键盘模拟 | 真实 Unitree 遥控器 |
| 安全 | 无风险 | 需保护架 |
