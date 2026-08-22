# A2 PC2–PiPER 完整部署方案

## 1. 最终架构

采用 **PC2 本地终止 CAN + ROS 2 语义级桥接**：

```text
笔记本（GPU、policy、A2 底盘命令）
192.168.123.10
        │ 一根千兆网线，接 A2 switch-1 / SDK 网段
        ├─────────────────────────────────────────────────────┐
        │                                                     │
A2 PC1 192.168.123.161                              A2 PC2 192.168.123.162
Unitree 底盘 DDS                                    piper_bridge container
                                                              │ SocketCAN can0
                                                              │ PiPER 原厂黑色 USB-CAN
                                                              └────────────── PiPER
```

PC2 不是 Linux 网桥，也不转发原始 CAN 帧。它在本机结束 PiPER SDK 链路，只向笔记本发布机器人级状态并接收机器人级目标。USB/CAN 时序、使能、反馈检查和 watchdog 因此不依赖笔记本调度；笔记本进程或网线失效时，停止决定仍由 PC2 本地执行。

### 为什么不转发原始 CAN

原始 CAN-over-Ethernet 会把 USB 驱动和 CAN 时序暴露到两个调度系统，并让笔记本、以太网和转发进程都进入停止链路。它还需要新增一套封包、排序、重连和超时协议，却不能改善当前 policy 所需的 6 关节状态/目标契约。

### 为什么不直接部署官方 `piper_ros`

官方 ROS package 是重要的接口参考，但当前已验证的任务使用 `C_PiperInterface_V2`、20 ms 高速电机速度平均、6 关节 50 Hz policy 契约和更严格的命令/反馈 watchdog。本 bridge 保留这条已验证的数据路径，同时只使用 ROS 2 标准消息，不引入 `piper_msgs` 自定义接口。

## 2. Control lifecycle

bridge 启动后为 `connected + disabled`。标准 lifecycle 是：

```text
read-only → resume（仅需要时）→ enable → fresh command stream
          → stop/watchdog fault → latched quick stop → explicit resume
```

`stop` 与 watchdog 都发送 PiPER quick-stop command；`resume` 显式发送 recovery command，确认 `arm_status=0` 后仍保持 disabled；`enable` 只在 feedback fresh/healthy 时开放，并清空任何旧 target。该顺序避免网络恢复或旧 publisher 自动带动机械臂。

## 3. 已知条件与待补实机信息

随附 A2 文档已经确认：

- PC2 是用户开发计算机，具有 `192.168.123.162` 和 `192.168.124.162`；
- `192.168.123.0/24` 是 switch-1 和 Unitree SDK/DDS 网段；
- 外置 USB-C 与用户 PC 相连；本机仍需用 `lsusb -t` 确认所选物理口 `[2]` 的实际拓扑；
- PC2 运行宇树导航服务，不应替换系统镜像或进行无边界的系统升级；复杂依赖优先放入 Docker。

以下信息不猜测，先标记为 `[待验证]`：

- PC2 的 OS、Docker、ROS、磁盘余量和真实网卡名；
- 本台 A2 能否通过端口 `[7]` 的单网线直接 SSH `192.168.123.162`；
- USB-C `[2]` 的稳定 USB bus address；
- 当前 A2 部署使用的 `ROS_DOMAIN_ID`；
- 宇树导航后台负载是否影响 PC2 的 50 Hz 周期。

安装前先收集只读报告：

```bash
bash ros2/Piper/scripts/collect_pc2_info.sh > pc2_piper_bridge_info.md
```

若单网线 SSH `192.168.123.162` 在本机不可用，首次安装使用文档明确给出的 PC2 管理口 `192.168.124.162`。运行时 DDS 仍走 `192.168.123.162`，因此最终控制拓扑仍是一根笔记本网线同时访问 PC1 和 PC2。

## 4. PC2 主机准备

### 4.1 安装物理链路

1. 断电后，将 PC2 USB-C `[2]` 通过支持数据的线/转接头连接到 PiPER 原厂 USB-CAN。
2. 按 PiPER 硬件手册连接 CAN-H/CAN-L 和机械臂供电。
3. 保证物理急停始终可触达。
4. 启动 A2 与 PiPER，确认 USB 设备：

```bash
lsusb
lsusb -t
```

插拔模块前后对比 `lsusb -t`，记录该接口的 USB bus address。

### 4.2 准备已验证的 PiPER SDK

bridge 明确依赖 `krushell/piper_sdk`，因为当前 manipulation 部署在此 fork 中加入了 `GetArmHighSpdInfoAverage`，用于保持任务所需的 20 ms 速度观测。

```bash
cd ~/projects
git clone https://github.com/krushell/piper_sdk.git
export PIPER_SDK_ROOT="$HOME/projects/piper_sdk"
```

bridge 运行期间，不得同时运行该仓库中的直接控制脚本或 `piper_ros` 控制节点。PiPER `can0` 只能有一个命令所有者。

### 4.3 在 host 激活 SocketCAN

PiPER 原厂模块固定使用 1 Mbit/s。直接复用 SDK 自带脚本，不再维护第二份 USB/CAN 配置逻辑：

```bash
cd <GeneralSim2Real>
PIPER_SDK_ROOT="$HOME/projects/piper_sdk" \
PIPER_CAN_NAME=can0 \
bash ros2/Piper/scripts/activate_can.sh
```

检查：

```bash
ip -details link show can0
candump can0
```

应能看到 PiPER 反馈帧。进入运动测试前结束临时 `candump` 终端，避免混淆日志。

若机器上有多个 USB-CAN，使用 SDK 打印的硬件端口固定映射：

```bash
PIPER_SDK_ROOT="$HOME/projects/piper_sdk" \
PIPER_CAN_NAME=can0 \
PIPER_USB_ADDRESS=1-2:1.0 \
bash ros2/Piper/scripts/activate_can.sh
```

示例中的 `1-2:1.0` 必须替换为目标 A2 实测值。

## 5. 构建并运行 PC2 bridge container

镜像固定为 Ubuntu 22.04 + ROS 2 Humble + CycloneDDS，并安装 bridge 与 `krushell/piper_sdk`。PC2 镜像不包含 Torch 和 policy；GPU 推理仍在笔记本。

```bash
cd <GeneralSim2Real>
bash ros2/Piper/docker/build_image.sh
```

若 PC2 无外网，可在 x86_64 笔记本构建后传输：

```bash
docker save doordog-piper-bridge:humble | gzip > doordog-piper-bridge_humble.tar.gz
scp doordog-piper-bridge_humble.tar.gz unitree@<pc2-management-ip>:~/
```

PC2 上导入：

```bash
gunzip -c ~/doordog-piper-bridge_humble.tar.gz | docker load
```

查找持有 `192.168.123.162` 的 PC2 网卡：

```bash
ip -br address
```

启动 bridge，将 `<pc2-123-interface>` 替换为实测网卡名：

```bash
PIPER_NET_IFACE=<pc2-123-interface> \
PIPER_CAN_NAME=can0 \
ROS_DOMAIN_ID=0 \
bash ros2/Piper/docker/run_bridge.sh
```

container 使用 host network，因此可直接访问 host 的 `can0` 和 `192.168.123.162`。节点启动后只连接、读状态并发布 diagnostics，默认不使能机械臂。

## 6. 笔记本准备

笔记本网卡保持 `192.168.123.10/24`，连接 A2 switch-1/SDK 口。先确认两个机器人计算单元都可达：

```bash
ping -c 2 192.168.123.161
ping -c 2 192.168.123.162
```

在现有 A2 ROS 2 workspace 中构建 package：

```bash
cd <GeneralSim2Real>/ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select piper_bridge
source install/setup.bash
```

CycloneDDS 必须绑定到当前 A2 已使用的物理网卡：

```bash
source <GeneralSim2Real>/ros2/Piper/scripts/use_ros2_interface.sh <laptop-123-interface>
export ROS_DOMAIN_ID=0  # 仅在现有 A2 部署使用其他值时替换
```

PiPER 不单独建立第二个 ROS domain。A2 和 PiPER 使用同一 domain；arm 的所有接口均位于 `/piper` namespace，不会与 `/lowstate`、`/lowcmd` 冲突。

## 7. 网络与只读验证

笔记本执行：

```bash
ros2 node list
ros2 topic hz /piper/joint_states
ros2 topic echo /piper/diagnostics --once
ros2 run piper_bridge piper_smoke_test
```

预期：

- 能看到 `/piper/piper_bridge`；
- `/piper/joint_states` 接近 50 Hz，包含 6 个 position 和 6 个 velocity；
- diagnostics 为 `connected and disabled`，且 `arm_status=0`；
- smoke 输出 `read-only smoke passed`，不发送任何运动命令。

## 8. 第一次运动 smoke

先确认机械臂当前位置到目标位置的路径安全。默认目标为全零位，与现有 `piper_learning.py` 测试一致：

```bash
ros2 run piper_bridge piper_smoke_test -- --move
```

client 会显式调用 enable、以 50 Hz 发布目标、检查 0.5° 误差，并在正常退出、异常或 Ctrl+C 时调用 `/piper/stop`。PiPER quick stop 是 latched state；下一次运动前必须由操作员显式调用 `/piper/resume`，成功后机械臂仍保持 disabled，再调用 enable。

若前一步已经触发 bridge quick stop，可使用显式组合参数：

```bash
ros2 run piper_bridge piper_smoke_test -- --move --resume-before-enable
```

随后单独验证 watchdog：运动命令开始后终止 publisher 或拔掉笔记本网线。PC2 应在约 `command_timeout_s=0.20` 秒内执行 quick stop；网络恢复后不得自动恢复运动。必须先显式 resume，再重新 enable；bridge 不自动清除 quick-stop state。

## 9. 通过 PC2 运行现有 krushell manipulation

使用此前已能直接运行 `krushell/piper_sdk` 和 Torch checkpoint 的笔记本 Python 环境。source ROS 2 与本 package 后执行：

```bash
ros2 run piper_bridge piper_krushell_manipulation -- \
  --checkpoint_path /absolute/path/to/checkpoint.pt \
  --device cuda:0 \
  --run_policy
```

保留的原任务参数：

```bash
--target_pos_b X Y Z
--random_target
--policy_steps N
--resume_before_enable  # 仅用于明确清除前一次 bridge quick stop
```

runner 加载原始 `Manipulation` 类，只在构造时把 `C_PiperInterface_V2` 替换为 `PiperSdkRos2Facade`。policy 网络、action scale、FK、keypoint observation、history 和 reset 逻辑仍由已经跑通的仓库提供。原任务中的 `MotionCtrl_2(..., speed=5, ...)` 映射为 PC2 bridge 固定参数 `speed_percent=5`；两边不一致时 adapter 会拒绝执行。

## 10. 与 A2 底盘同时运行

笔记本继续按已经验证的 `GeneralSim2Real/ros2/A2` 路径运行底盘。共享资源仅为笔记本物理网卡、CycloneDDS 流量和 ROS domain：

- PC1 继续独占 A2 底盘控制；
- PC2 独占 PiPER USB-CAN；
- GPU policy 在笔记本统一读取最新 base/arm state，统一产生 base/arm action；
- 不把 arm 命令绕经 PC1，也不在 PC1 安装 PiPER SDK。

组合运动前先保持底盘命令为零并检查：

```bash
ros2 topic hz /lowstate
ros2 topic hz /piper/joint_states
ros2 topic echo /piper/diagnostics --once
```

两条状态链都稳定后，再进行低速、有人看护的 whole-body 测试。

## 11. 停止与关机

正常停止：

```bash
ros2 service call /piper/stop std_srvs/srv/Trigger '{}'
ros2 service call /piper/disable std_srvs/srv/Trigger '{}'
```

`/piper/stop` 使用 PiPER quick stop，后续再次运动前需要显式 recovery：

```bash
ros2 service call /piper/resume std_srvs/srv/Trigger '{}'
ros2 service call /piper/enable std_srvs/srv/Trigger '{}'
```

`resume` 只允许在 bridge motion disabled 且 arm status 为 normal/quick-stop 时执行；它不会自动 enable，也不会恢复旧 command。随后停止 bridge container。若 ROS/DDS 已不可用，使用物理急停；任何以太网命令都不能作为最后一级安全手段。

## 12. 实机跑通后再增加的能力

以下内容不进入第一版，以免用未验证复杂度替代可工作的端到端链路：

- 根据实测 USB bus address 固化 `can0`；
- 将已验证 container 变成 PC2 system service，但仍保持启动后 disabled；
- 在宇树导航后台运行时记录 PC2 CPU 与 DDS jitter，必要时再做 CPU affinity；
- policy 真正需要时再增加 gripper 独立接口；
- 多机日志需要关联时再部署时间同步。控制 watchdog 使用本机 monotonic receipt time，不依赖跨机时钟同步。
