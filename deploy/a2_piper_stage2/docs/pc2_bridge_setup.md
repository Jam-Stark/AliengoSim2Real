# PC2 PiPER bridge setup and verification

PC2 是 PiPER CAN 的唯一拥有者。Policy host 只收发 semantic ROS messages，不打开 USB device、SocketCAN 或 raw CAN。

2026-08-24实机只读盘点及随后获批的bootstrap/CAN/bridge结果见[PC2_READONLY_20260824.md](../../../ros2/Piper/docs/PC2_READONLY_20260824.md)。当前`can_piper`为1 Mbit/s UP/ERROR-ACTIVE，command-gate-closed bridge运行，实时joint state 50 Hz且ros-readonly Gate PASS；尚未执行任何motion service或command。

PC2没有default route。后续依赖与image应在m45准备后经`.123.162`传入；不要为了安装方便临时改变PC2 route。

## Repository interface contract

| Interface | Type | Semantics |
| --- | --- | --- |
| `/piper/joint_states` | `sensor_msgs/msg/JointState` | `arm_j1..arm_j6`，position rad |
| `/piper/joint_command` | `trajectory_msgs/msg/JointTrajectory` | 恰好一个 point，absolute rad |
| `/piper/diagnostics` | `diagnostic_msgs/msg/DiagnosticArray` | bridge/device/fault state |
| `/piper/resume` | `std_srvs/srv/Trigger` | explicit fault-latch recovery；不自动打开 command gate |
| `/piper/enable` | `std_srvs/srv/Trigger` | motor enable + command gate protocol |
| `/piper/stop` | `std_srvs/srv/Trigger` | local software quick stop |
| `/piper/disable` | `std_srvs/srv/Trigger` | disable path |

Repository defaults：

```text
control rate:      50 Hz
command timeout:   0.20 s
feedback timeout:  0.50 s
joint order:       arm_j1..arm_j6
gripper:           absent
```

这些是 interface/code facts；hardware feedback仍未建立。

## Site evidence to collect first

只读记录以下内容，不改变 device 或 CAN 配置：

- PC2 hostname、OS、kernel、architecture、ROS distribution；
- 实际 `krushell/piper_sdk` 路径、revision、dirty status 和启动方式；
- USB-CAN vendor/product/serial 与稳定设备规则；
- SocketCAN interface name、state、bitrate；
- PiPER model、firmware/API status；
- bridge container/image、launch command、ROS domain、network interface；
- `/piper/*` topic/service type、publisher/subscriber count、QoS；
- joint state names/order、units、rate、jitter、timestamp/receipt behavior；
- diagnostics idle state与已有 fault latch。

Read-only examples：

```bash
uname -a
ip -details link show can_piper
ros2 topic list -t | grep '^/piper/'
ros2 topic info -v /piper/joint_states
ros2 topic type /piper/joint_states
ros2 topic hz /piper/joint_states
ros2 topic echo --once /piper/joint_states
ros2 topic echo --once /piper/diagnostics
ros2 service list -t | grep '^/piper/'
```

本次现场实际是`can0 DOWN/STOPPED`且没有bitrate；已按本条停止。如果现场 interface 不是 `can_piper` 或 bitrate 不是预期值，只记录差异并停止。不得由 AI 自行改名、执行 link setup script 或改变 bitrate。

## CAN ownership

接受标准：

- 只有一个 PC2 bridge process 打开 PiPER CAN；
- policy host、第二个 SDK demo、diagnostic tool 不竞争 command ownership；
- container 使用 host network/SocketCAN，是否需要额外 capability 由现场已验证 launch 决定；
- raw CAN 不通过 DDS 转发；
- SDK unit conversion只在 bridge发生一次。

## Hardware validation sequence

1. USB-CAN、SocketCAN、SDK 与 bridge process存在性检查；无动作。
2. `/piper/joint_states` 和 diagnostics只读连续观测；无动作。
3. 使用既有、已验证的 PiPER control program做低速低幅基线；此时 Stage2 不发布。
4. 驱动禁能状态解析 semantic command，确认 joint names、absolute radians 和 one-point rule。
5. 支撑/隔离状态逐关节验证 direction、zero、limit；一次只验证一个关节。
6. 受控验证 enable → fresh command → stop，以及 command timeout、feedback loss、explicit resume。
7. 记录 hardware/site limits，与 LMP URDF limits取交集。

所有 motion steps 都需要现场 operator、物理急停和对应 Gate 许可。Software quick stop 不是 safety-rated emergency stop。

## Stop and recovery semantics

Repository design要求：bridge 启动时 command gate关闭；enable成功后仍必须收到 fresh command。Command/feedback fault由 PC2本地 quick stop并 latch。恢复必须显式 resume，且恢复后 command gate仍关闭，网络重连不能自动恢复运动。

这些行为在实机确认前只能标记 `TO_VERIFY`。如果实际 bridge行为不同，停止后续 Gate并修正文档/config，不得在 policy host 添加 retry/fallback掩盖差异。
