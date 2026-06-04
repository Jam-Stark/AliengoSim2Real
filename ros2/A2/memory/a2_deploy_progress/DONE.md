# DONE

## 2026-06-04 14:34 HKT

- [x] 确认项目根目录没有顶层 `MEMORY.md`，因此原项目没有可用 root memory 入口。
- [x] 新建 `ros2/A2` 独立 A2 deployment workspace area。
- [x] 新建 `a2_lowlevel` ament package，依赖 `ament_cmake`、`rclcpp`、`unitree_hg`。
- [x] 实现标准 A2 12-motor order：
  - `FR_BODY`
  - `FR_THIGH`
  - `FR_CALF`
  - `FL_BODY`
  - `FL_THIGH`
  - `FL_CALF`
  - `RR_BODY`
  - `RR_THIGH`
  - `RR_CALF`
  - `RL_BODY`
  - `RL_THIGH`
  - `RL_CALF`
- [x] 实现 `A2LowLevelInterface`：
  - subscribe `rt/lowstate`
  - publish `rt/lowcmd`
  - latest state snapshot
  - fresh-state guard
  - safe zero command
  - 12-joint command publishing
- [x] 实现 A2 独立 CRC，不复用 Go2W `motor_crc.cpp`。
- [x] 实现 `a2_lowlevel_smoke`：
  - 默认 listen-only
  - `publish_zero`
  - `stand_test`
  - `state_timeout_ms`
  - `command_hz`
- [x] 编写 `ros2/A2/README.md`，记录 build/run、topic/type、12 motor order、安全提醒和 policy boundary。
- [x] 在 code machine 的 `~/third_party/unitree` clone：
  - `unitree_ros2`
  - `unitree_sdk2`
  - `unitree_sdk2_python`
- [x] 只读确认 `unitree_ros2` 中存在 `unitree_hg/msg/LowCmd.msg`、`LowState.msg`、`MotorCmd.msg`，字段名与当前 A2 adapter 假设一致。
- [x] 实现 `ros2/A2/scripts/collect_deploy_machine_info.sh`，用于部署机生成 `DeployMachineINFO.md`。
- [x] 本地验证：
  - `xmllint --noout ros2/A2/package.xml` 通过。
  - `bash -n ros2/A2/scripts/collect_deploy_machine_info.sh` 通过。
  - `bash ros2/A2/scripts/collect_deploy_machine_info.sh --help` 通过。
  - `bash ros2/A2/scripts/collect_deploy_machine_info.sh > /tmp/a2_deploy_machine_info_smoke.md` 在 macOS / non-ROS 环境生成 Markdown report。

## 2026-06-04 14:48 HKT

- [x] 将 A2 memory 规范化为 root memory schema：`description.md` 增加 required YAML frontmatter 和 required sections。
- [x] 保留 A2 low-level adapter、smoke node、deploy machine info collector、部署机 blocker 和 TODO/DONE summary 事实。
- [x] A2 SDK/reference docs 只引用 `ros2/A2_Guide/`，未复制长文档到 memory。
