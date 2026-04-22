测试框架已创建完成。新增 3 个文件，修改 1 个文件：

| 文件                                                                                      | 功能                                                                |
| ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| [`ros1/src/test/fake_low_state_publisher.cpp`](ros1/src/test/fake_low_state_publisher.cpp) | 500Hz 合成 LowState 发布器，带键盘按键模拟遥控器                    |
| [`ros1/src/test/low_cmd_monitor.cpp`](ros1/src/test/low_cmd_monitor.cpp)                   | low_cmd 订阅监视器，验证 NaN/超限/频率/对称性                       |
| [`ros1/launch/test_deploy.launch`](ros1/launch/test_deploy.launch)                         | 测试 launch 文件（deploy + monitor）                                |
| [`ros1/CMakeLists.txt`](ros1/CMakeLists.txt:125)                                           | 添加了 `fake_low_state_publisher` 和 `low_cmd_monitor` 编译目标 |

### 同步到远程机后重新编译

macOS 上：

```bash
rsync -avz ros1/ dreams@<IP>:~/Downloads/WorkSpace/AliengoSim2Real/ros1/
```

Docker 容器内：

```bash
cd /root/catkin_ws
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```


### Docker 容器内重新编译

```bash
cd /root/catkin_ws
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

编译成功后应该看到 3 个可执行文件：

```bash
ls /root/catkin_ws/devel/lib/aliengo_deploy/
# aliengo_deploy  fake_low_state_publisher  low_cmd_monitor
```

### 然后运行测试

终端 1：

```bash
roslaunch aliengo_deploy test_deploy.launch \
    policy_path:=/work/AliengoSim2Real/policy/aliengo/
```

终端 2（`docker exec -it noetic-gpu bash`）：

```bash
source /opt/ros/noetic/setup.bash && source /root/catkin_ws/devel/setup.bash
rosrun aliengo_deploy fake_low_state_publisher
```

在终端 2 中按 `a` 使能策略，按 `w` 发速度指令，按 `b` 停止。把编译和运行结果发给我。

| 按键        | 动作                       |
| ----------- | -------------------------- |
| `a`       | 使能策略（模拟遥控 A 键）  |
| `b`       | 受控停止（模拟遥控 B 键）  |
| `e`       | 紧急制动（模拟 L2+B）      |
| `w`/`x` | 增加/减少 vx 命令          |
| `d`/`c` | 增加/减少 wz 命令          |
| `s`       | 清零速度指令（模拟 Start） |
| `r`       | 重置策略（模拟 Select）    |
| `0`       | 摇杆归零                   |
| `q`       | 退出                       |

### 预期验证结果

1. **启动时**：deploy 节点显示 `Waiting for low_state...`，monitor 显示 `Waiting for messages...`
2. **fake publisher 启动后**：deploy 打印 `First low_state received.`，monitor 开始显示 12 电机数据（全部 Kp=0，零力矩模式）
3. **按 `a`**：deploy 打印 `Policy ENABLED`，monitor 看到 12 电机 Kp 变非零、q 值开始变化
4. **按 `w`**：给前进速度指令，动作输出应变化
5. **按 `b`**：deploy 打印 `Controlled STOP`，monitor 看到 Kp=60/Kd=5 的趴下过程
6. **按 `e`**：deploy 打印 `EMERGENCY DAMPING STOP`，monitor 看到 Kp=0/Kd=3
7. **全程**：monitor 的 NaN 计数维持为 0，无关节超限

当前 shell 的**临时修复命令**如下，直接在容器里执行即可。

我现在收敛到 2 个最可能原因：

1. 主机名 `noetic-gpu` 不能自解析（你的 `/etc/hosts` 里没有它）
2. ROS1 启动时优先拿主机名做回连地址，而不是 localhost

这和 [`test_deploy.launch`](ros1/launch/test_deploy.launch) 或 [`AliengoDeployNode::start()`](ros1/src/aliengo_deploy_node.cpp:64) 无关，问题发生在 ROS 网络初始化阶段。

先在**当前容器 shell**里执行这组命令：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
unset ROS_IP

echo "127.0.0.1 $(hostname)" >> /etc/hosts

source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

env | grep '^ROS_' | sort
roslaunch aliengo_deploy test_deploy.launch \
  policy_path:=/work/AliengoSim2Real/policy/aliengo/
```

如果你想更稳一点，也可以先单独验证 ROS master：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
unset ROS_IP

echo "127.0.0.1 $(hostname)" >> /etc/hosts
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash

roscore
```

然后开第二个容器 shell，再执行：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
unset ROS_IP
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
roslaunch aliengo_deploy test_deploy.launch \
  policy_path:=/work/AliengoSim2Real/policy/aliengo/
```

如果这组命令能跑通，就说明诊断成立：**根因就是主机名回解析/ROS 网络变量问题**。下一步再把这个临时修复固化进 [`ros1/README.md`](ros1/README.md) 和 [`ros1/scripts/setup_and_build.sh`](ros1/scripts/setup_and_build.sh) 就行。






当前状态是**正常的、而且已经推进到下一阶段**。

### 你这段日志说明了 4 件事

1. 你的策略已经正确加载，`Tuple(action, pred_est)` 也已经修好。这个从 [`Policy::get_action()`](utils/cpp_manager_env/net.cpp:1699) 的日志能看到：返回了 2 元组，并成功提取了第 0 项 action。
2. 1472 维观测对齐正确，[`ManagerBasedEnv::init_manager()`](utils/cpp_manager_env/ManagerEnv.cpp:226) 的 dummy inference 已经通过。
3. 部署节点已经正常启动，进入了 [`AliengoDeployNode::start()`](ros1/src/aliengo_deploy_node.cpp:64) 之后的等待状态。
4. 你现在**还没启动 fake 状态发布器**，所以 [`AliengoDeployNode::controlLoop()`](ros1/src/aliengo_deploy_node.cpp:118) 会一直走“无 low_state”分支，并通过 [`AliengoDeployNode::zeroLowCmd()`](ros1/src/aliengo_deploy_node.cpp:164) 发布安全空命令。

所以监视器里看到的：

- `q = 2146000000`
- `dq = 16000`
- `Kp = 0`
- `Kd = 0`

这不是错误，而是 PosStopF / VelStopF 的**哨兵值**，表示“当前不下发实际位置控制”。

### 你现在要做的唯一下一步

在**新的容器终端**里启动 fake 状态发布器（不是在当前 `roslaunch` 那个终端里）。

按顺序执行：

1. export ROS_MASTER_URI=http://127.0.0.1:11311
2. export ROS_HOSTNAME=127.0.0.1
3. source /opt/ros/noetic/setup.bash
4. source /root/catkin_ws/devel/setup.bash
5. rosrun aliengo_deploy fake_low_state_publisher

这个可执行文件来自 [`ros1/src/test/fake_low_state_publisher.cpp`](ros1/src/test/fake_low_state_publisher.cpp)。

### 启动 fake publisher 之后，你应该看到的现象

在 `roslaunch` 那个终端里：

- 出现 `First low_state received.`
- [`low_cmd_monitor`](ros1/src/test/low_cmd_monitor.cpp) 不再只显示哨兵值
- 关节 `q` 会回到接近默认站立位姿的有限数值

在 fake publisher 终端里：

- 按 `a`：触发 enable，走 [`processRemoteButtons()`](ros1/src/aliengo_deploy_node.cpp:425) 的 A 键逻辑
- 按 `w`：增加前进命令
- 按 `b`：触发 stop posture
- 按 `e`：触发紧急阻尼停机

### 你接下来应该重点观察的 3 个结果

1. **按 `a` 之前**：监视器里 `Kp=0`，说明系统处于安全 idle。
2. **按 `a` 之后**：12 个关节出现正常有限 `q` 值，且 `Kp` 变为非零，说明策略输出真正接管。
3. **按 `b` / `e` 之后**：输出切回 stop / damping 模式，说明 enable-stop-reset 流程打通。

### 结论

你现在已经不是“卡住”，而是已经完成了：

- 模型加载
- 观测维度对齐
- TorchScript tuple 返回修复
- ROS launch 启动
- 低层输出监视

当前差的只剩一步：**把 fake low_state 真正喂进去**。启动 [`ros1/src/test/fake_low_state_publisher.cpp`](ros1/src/test/fake_low_state_publisher.cpp) 后，这套 test framework 就进入完整联调阶段。
