# PiPER bridge 分层实机验证

验证顺序从被动观测逐步增长到现有 policy。不要从未验证环境直接跳到 whole-body motion。

## Gate A：主机与物理链路

- 保存 `collect_pc2_info.sh` 输出；
- 通过插拔前后的 `lsusb -t` 确认所选 USB-C 确实连接 PC2；
- 确认 `can_piper` 为 `UP`、1 Mbit/s，且 `candump can_piper` 有 PiPER 反馈；
- 确认没有其他 PiPER SDK 或 `piper_ros` 控制进程；
- 确认物理急停可触达。

CAN 无反馈，或 CAN 名称每次重启变化时，不进入下一层。

## Gate B：bridge 被动状态

启动 PC2 bridge，在笔记本执行：

```bash
ros2 topic hz /piper/joint_states
ros2 topic echo /piper/diagnostics --once
ros2 run piper_bridge piper_smoke_test
```

满足以下条件后继续：状态稳定接近 50 Hz，6 个速度窗口 sample count 持续非零，`arm_status=0`，只读 smoke 未产生运动。

## Gate C：先验证停止路径

机械臂周围清空后：

1. 运行 `ros2 run piper_bridge piper_smoke_test -- --move --hold-current`；该命令只保持测得的启动姿态，并在退出路径调用 `/piper/stop`；
2. diagnostics 应显示 `manual_stop`、`command_gate_open=false`，PiPER status 进入 quick-stop；
3. 调用 `/piper/resume`，确认 status 回到 normal、`command_gate_open=false`，并现场确认机械臂没有恢复旧目标运动；
4. 再次 enable 并开始命令后拔掉笔记本网线，PC2 必须因 command timeout 停止；
5. 重新接线后不得恢复运动；必须显式 resume，再重新 enable；
6. 断开 PiPER USB-CAN 或机械臂反馈，PC2 必须因 feedback watchdog 停止。

这一层的核心不是“能动”，而是证明停止决定在 PC2 本地完成。

## Gate D：已知关节目标

```bash
ros2 run piper_bridge piper_smoke_test -- --move
# 若 Gate C 留下 quick-stop state：
ros2 run piper_bridge piper_smoke_test -- --move --resume-before-enable
```

机械臂应在设定超时内进入目标容差；状态持续新鲜；正常退出与 Ctrl+C 都触发 stop。

## Gate E：现有 manipulation policy

使用直接 CAN 部署时相同的 checkpoint、target 和 GPU 环境。只有前一层留下 quick-stop state 时才加入 `--resume_before_enable`：

```bash
ros2 run piper_bridge piper_krushell_manipulation -- \
  --checkpoint_path /absolute/path/to/checkpoint.pt \
  --device cuda:0 \
  --run_policy
# 前一层留下 quick-stop state 时，在命令末尾追加 --resume_before_enable
```

不要只看视频成功与否，至少比较：

- joint position 和 20 ms velocity trace；
- policy step period 与 missed deadline；
- 原任务打印的最终 position/orientation/keypoint error；
- bridge command/feedback watchdog 事件；
- PiPER status code 是否出现异常。

## Gate F：A2 与 PiPER 联合

启动已经验证的 A2 底盘链路，初始 base command 保持为零。先确认 `/lowstate` 与 `/piper/joint_states` 同时稳定，再进行低速组合运动；现场保留看护人员和物理急停。

实机完成后，将以下结果补回仓库：

- PC2 OS、Docker 与网卡名；
- USB-C `[2]` 的 bus address 和稳定 CAN 映射；
- 实际共享 `ROS_DOMAIN_ID`；
- laptop↔PC2 state rate/jitter 与 command-stop latency；
- 宇树导航后台运行时的 CPU 占用；
- 单网线 SSH `192.168.123.162` 是否稳定。
