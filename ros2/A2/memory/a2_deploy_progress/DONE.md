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
- [x] 在 code machine 的 `/Users/caobaoquan/Downloads/python/projects/third_party/unitree` clone：
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

## 2026-06-04 19:31 HKT

- [x] 将 Unitree reference repos memory 路径从旧 home-level default path 更新为 parent-projects layout：`/Users/caobaoquan/Downloads/python/projects/third_party/unitree`。
- [x] 更新部署机 TODO 命令，使用 `UNITREE_ROOT="$(cd .. && pwd)/third_party/unitree"` 并显式传给 `collect_deploy_machine_info.sh --unitree-root`。

## 2026-06-05 15:07 HKT

- [x] 实现 A2 Policy Adapter v1 optional CMake target：默认不查找 LibTorch/jsoncpp，`-DBUILD_A2_POLICY_DEPLOY=ON` 时才 build `a2_policy_deploy`。
- [x] 新增 `A2PolicyDeployNode`，通过 shared `ManagerBasedEnv` / `Policy` runtime 加载 `policy/A2_policy/policy.pt`，并读取 `policy/A2_policy/policy.json` 做 contract validation。
- [x] 定义 A2 policy observation contract：每帧 `46` dims、history `32`、flatten `1472`，包含 projected gravity、base angular velocity、joint q/dq、last raw action、gait clock、static command。
- [x] 定义 action contract 和 mapping：training joint order 到 A2 low-level order same signs / no inversion，raw action clip 后按 `default_joint_pos + 0.25 * raw_action` 生成 target q，hip/thigh/calf 使用固定 PD gains。
- [x] 保持 low-level publish boundary：policy node 只调用 `A2LowLevelInterface::publish_joint_commands()`，不直接写 `unitree_hg::msg::LowCmd`，不绕过 fresh-state guard、mode routing 或 A2 CRC。
- [x] 增加 policy safety gating：missing/stale state、history 未 warm、`enable_motion=false`、NaN/Inf、wrong observation/action dim 时拒绝发布 motion。
- [x] 更新 `ros2/A2/README.md` 的 policy build/run、contract、mapping、safety 和 remote TODO。

## 2026-06-05 16:52 HKT

- [x] 实现 A2 remote decode utility：从 `wireless_remote[40]` 按 Unitree SDK2 sample offsets `lx=4`、`rx=8`、`ry=12`、`ly=20` decode little-endian `float32`，并按 byte `2/3` bit layout decode buttons。
- [x] remote decode 增加 `deadzone`、`[-1,1]` clamp 和 NaN/Inf invalid guard；invalid stick data 不进入 policy command。
- [x] `a2_lowlevel_smoke` 增加 `log_remote` listen-only logging，打印 sticks 和 button names，默认仍不发布 command。
- [x] `a2_policy_deploy` 增加 `command_source=static|remote`，保留 static command params，并实现 remote mapping、`L2` gate、`Select` / `L2+B` local stop、runtime reset 和 `publish_zero()`。
- [x] 更新 `ros2/A2/README.md` remote build/run、button gate、joystick mapping、safety limits 和 deploy machine validation checklist。
