# Stage2 deployment architecture

## 已选定的最终拓扑：A2 direct mode

```text
                 same wired A2 ROS 2 / DDS domain
┌────────────────────────────────────────────────────────────┐
│ policy host: a2_piper_stage2_direct C++ process            │
│                                                            │
│ A2LowLevelInterface::latest_state()                         │
│   -> training mapping / synchronized PiPER snapshot         │
│   -> arm actor -> body plan -> dog preview -> dog actor     │
│   -> A2LowLevelInterface::publish_joint_commands()          │
│                         │                                  │
│                         └─ /piper/joint_command (semantic)  │
└──────────────────────┬──────────────────────────┬───────────┘
                       │ /lowcmd                 │ ROS 2
                       ▼                         ▼
             ┌──────────────────┐      ┌─────────────────────┐
             │ A2 actuators     │      │ A2 PC2 PiPER bridge│
             │ existing mode/   │      │ CAN sole owner     │
             │ CRC/PD boundary  │      │ watchdog/stop      │
             └──────────────────┘      └──────────┬──────────┘
                                                  ▼
                                                PiPER
```

## Ownership boundary

Stage2 direct process拥有高层 policy tick，并复用 main 分支已有 A2 low-level boundary：

- 合并 A2 attitude、12 个腿关节状态和 PiPER j1–j6 状态；
- 构造 dog/arm observation histories；
- 运行 arm-first dual policy；
- 生成training-order position target，并按固定mapping写入既有 `A2JointCommand`；
- 执行 policy-side hard/rate limiting、时序监督和日志。

A2 low-level adapter继续拥有：

- raw `unitree_hg` state/command；
- CRC、mode routing、PD command 和高频执行器闭环；
- `A2LowLevelInterface::publish_joint_commands()` 这一唯一 raw command boundary。

已复用的同一台A2事实：raw motor order为 `FR, FL, RR, RL`（各腿body/thigh/calf）；training→raw index为 `[3,0,9,6,4,1,10,7,5,2,11,8]`；raw quaternion extraction为 `wxyz`；hip/thigh PD为`140/5`、calf为`220/9`；fresh state timeout为`200 ms`。

PC2 继续作为 PiPER CAN 唯一拥有者：

- 只有 PC2 bridge 打开 PiPER USB-CAN/SocketCAN；
- laptop 不打开 raw CAN，不转发 raw CAN frame；
- SI unit 到 SDK unit 的转换只能在 bridge 执行一次；
- command timeout、feedback timeout、quick stop 和 explicit resume 在 PC2 本地执行。

## 当前实现状态

| Component | Repository status | Hardware status |
| --- | --- | --- |
| Policy contract/parser | 实现；直接读取 LMP source manifest | offline only |
| TorchScript actors | 模型/reference 已独立静态核实 | 未连接机器人 |
| Observation/history/runtime | arm-first、same-tick preview 已实现 | deterministic mock only |
| Named action processing | LMP limit + per-tick rate limit 已实现 | site/hardware limits `TO_VERIFY` |
| PiPER semantic interface | repository code contract 已核对 | hardware-validation pending |
| A2 direct control | C++ node复用main mapping与`A2LowLevelInterface` | staged hardware Gates pending |

Python `ros-shadow/ros-live` external-semantic mode仍不可用；正式shadow/live使用C++ direct node。Direct node不手工构造raw message，唯一调用既有`publish_joint_commands()`。

## Logical policy tick

每个 `20 ms` logical tick 必须使用同一个 synchronized robot snapshot：

```text
receive/validate one snapshot
        │
        ├─ append arm frame using previous raw arm control action
        └─ append committed dog frame using previous committed plan
        │
        ▼
run arm actor on committed 30x20 history
        │
        ├─ arm control = output[0:6]
        └─ plan = clip(output[6:8], -1, 1) * 0.4
        │
        ▼
rebuild same-state dog preview frame with the new plan
        │
        ▼
run dog actor on committed frames[1:30] + preview
        │
        ▼
cache raw actions and plan for the next snapshot
        │
        ▼
nominal target -> named hard limits -> rate limits -> semantic bridges
```

Preview 只用于当前 dog inference，不能修改 persistent dog history。`last_action` 缓存 raw actor output，不能缓存 limited target。

## State and time contract

Policy input 需要：

- A2 raw IMU `wxyz` body-to-world attitude，与main同路径提取；Stage2仍在read-only Gate核对projected gravity；
- A2 12-joint position/velocity；
- PiPER j1–j6 position；
- local monotonic receipt time，以及能够检查 state age/skew 的 timestamp contract；
- locomotion `[vx, vy, yaw_rate]` 与 arm goal `[radius, elevation_pitch, yaw]`。

Actor 不使用 base linear/angular velocity、PiPER velocity、TCP pose、contact、torque/current 或 critic privileged signals。

C++ direct node用local steady receipt time同步两路snapshot，默认检查A2/PiPER age `200 ms`与skew `50 ms`，超限发布`state=blocked`并停止policy output。PiPER command/feedback watchdog分别由PC2以`200/500 ms`持有。

## Failure containment

Network 或 policy host 故障不能只依赖 laptop 停止发布：

- A2继续使用既有fresh-state拒绝发布与remote stop/controlled-down路径；现场Gate必须确认node/process/network故障后的实机结果；
- PC2 bridge 必须在 command/feedback timeout 后本地停止并 latch；
- 恢复不得因为网络重新连接而自动运动；
- hardware emergency stop 必须独立于 laptop、ROS 2 和 Python runtime。
