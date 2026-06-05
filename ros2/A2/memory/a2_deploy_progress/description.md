---
name: a2_deploy_progress
scope: ros2/A2
status: active
last_updated: "2026-06-05 15:07 HKT"
owned_paths:
  - ros2/A2/
  - ros2/A2_Guide/
read_when:
  - 修改 A2 ROS2 low-level deployment、A2 policy deploy、unitree_hg interface、rt/lowstate/rt/lowcmd routing 或 deploy machine readiness 时
---

## Purpose

本 entry 记录 `ros2/A2` 的独立 A2 ROS2 low-level deployment 起点。当前 A2 work 不修改既有 Go2W `ros2/src/**` 链路。

已完成事实：

- 新建独立 ament package `a2_lowlevel`。
- 实现标准 A2 12-motor low-level adapter：订阅 `rt/lowstate`，发布 `rt/lowcmd`，使用 `unitree_hg` ROS2 messages。
- 实现 `A2LowLevelInterface` public API：`latest_state()`、`has_fresh_state()`、`publish_zero()`、`publish_joint_commands()`。
- 实现 A2 专属 CRC，未复用 Go2W `motor_crc`。
- 实现 `a2_lowlevel_smoke`，默认 listen-only，`publish_zero` / `stand_test` 需要显式参数。
- 实现可选 `a2_policy_deploy` target：`BUILD_A2_POLICY_DEPLOY=ON` 时才查找 LibTorch/jsoncpp，并通过 shared `ManagerBasedEnv` / `Policy` runtime 加载 `policy/A2_policy/policy.pt`。
- `a2_policy_deploy` 校验 `policy/A2_policy/policy.json` contract：`action_dim=12`、`per_frame_obs_dim=46`、`history_length=32`、`action_scale=0.25`、`sim_dt=0.005`、`control_decimation=4`、训练 joint order。
- A2 policy observation 每帧为 `projected_gravity_xy(2)`、`base_ang_vel(3)*0.25`、`joint_q-default_pos(12)`、`joint_dq*0.05`、`last_raw_action(12)`、`gait_clock(2)`、`command(3)*[2,2,0.25]`，history `32` flatten 为 `1472`。
- A2 policy action 先按 `action_clip` clip，再映射为 `default_joint_pos + action_scale * raw_action`；训练顺序到 A2 low-level order same signs / no inversion，低层发布只走 `A2LowLevelInterface::publish_joint_commands()`。
- 实现部署机信息采集脚本 `ros2/A2/scripts/collect_deploy_machine_info.sh`，用于生成 `DeployMachineINFO.md`。
- 当前 code machine 的 Unitree reference repos 已移动到 `/Users/caobaoquan/Downloads/python/projects/third_party/unitree`，即 `AliengoSim2Real` 同级 parent `projects` 下的 `third_party/unitree`；部署机也计划使用同样的 parent-projects layout。

当前 blocker：

- code machine 没有 ROS2 / `colcon` / `/opt/ros`，无法本地完整 build `a2_lowlevel`。
- A2 CRC 仍需和部署机 `unitree_hg` generated messages、Unitree SDK2 sample 或实机 low-level command 行为对照验证。
- 低层实机控制前必须确认 Unitree 内置运动服务 `ai_sport` / `ai_sports` 已关闭；当前首版只在 README / smoke log 提醒，未自动调用 `MotionSwitcherClient`。

## When Codex/AI Should Read This Entry

- 修改 A2 `a2_lowlevel` package、low-level adapter、CRC、smoke node 或 deploy machine info collector。
- 修改 A2 policy deploy、policy contract、observation/action mapping、ManagerBasedEnv adapter boundary。
- 需要判断 A2 low-level deployment 当前 blocker、部署机验证步骤或安全前置条件。

## Source Paths

- `ros2/A2/package.xml`
- `ros2/A2/CMakeLists.txt`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_policy_deploy_node.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/src/a2_lowlevel_interface.cpp`
- `ros2/A2/src/a2_policy_deploy_node.cpp`
- `ros2/A2/src/a2_crc.cpp`
- `ros2/A2/src/a2_lowlevel_smoke.cpp`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/README.md`
- `policy/A2_policy/policy.pt`
- `policy/A2_policy/policy.json`
- `ros2/A2_Guide/`

## TODO Summary

- 等部署机运行 `collect_deploy_machine_info.sh` 生成 `DeployMachineINFO.md` 后，基于真实 ROS2 / Unitree / network / message interface 信息修正部署链路。
- Unitree reference repos 统一按 `AliengoSim2Real` 同级 parent `projects` 下的 `third_party/unitree` 查找；运行 deploy info collector 时传入 `--unitree-root`，避免回退到旧 home-level default path。
- 在部署机 build `a2_lowlevel`，验证 `unitree_hg` include/type/field names 和 CRC。
- 首次实机前确认 `ai_sport` / `ai_sports` 关闭、离地或限功率 smoke、hardware emergency stop。
- 在部署机安装/配置 LibTorch + jsoncpp 后，用 `-DBUILD_A2_POLICY_DEPLOY=ON` build `a2_policy_deploy` 并验证 TorchScript runtime。
- A2 remote control 目前只保留 `wireless_remote[40]` snapshot 输入边界；后续如需 remote command，需要新增 decode、button gate 和 stick mapping。

## DONE Summary

- A2 low-level adapter、smoke node、deploy machine info collector、README 首版已完成。
- A2 memory 已规范化为 root memory schema，并只引用 `ros2/A2_Guide/`，不复制长 A2 SDK docs。
- Unitree reference repos 路径已从旧 home-level default path 更新为 parent-projects layout：`/Users/caobaoquan/Downloads/python/projects/third_party/unitree`。
- A2 Policy Adapter v1 已实现：guarded CMake target、`A2PolicyDeployNode`、policy contract validation、observation/action mapping、安全 publish gating、README policy sections。

## Recommended Next Files To Read

- `ros2/A2/README.md`
- `ros2/A2/scripts/collect_deploy_machine_info.sh`
- `ros2/A2/include/a2_lowlevel/a2_lowlevel_interface.h`
- `ros2/A2/include/a2_lowlevel/a2_crc.h`
- `ros2/A2/memory/a2_deploy_progress/TODO.md`
- `ros2/A2/memory/a2_deploy_progress/DONE.md`
- `memory/shared_policy_runtime/description.md`
