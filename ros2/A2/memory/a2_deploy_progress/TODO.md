# TODO

## 2026-06-04 14:34 HKT

- [ ] 在 A2 部署机运行：

  ```bash
  mkdir -p ~/third_party/unitree
  cd ~/third_party/unitree

  git clone https://github.com/unitreerobotics/unitree_ros2
  git clone https://github.com/unitreerobotics/unitree_sdk2
  git clone https://github.com/unitreerobotics/unitree_sdk2_python
  安装unitree 官方SDK


  bash ros2/A2/scripts/collect_deploy_machine_info.sh --ping > DeployMachineINFO.md
  ```
  将 `DeployMachineINFO.md` 回传给 Codex，用于调整 A2 deployment chain。
- [ ] 在部署机确认 `~/third_party/unitree` 存在：

  - `unitree_ros2`
  - `unitree_sdk2`
  - `unitree_sdk2_python`
- [ ] 在部署机 source ROS2 + Unitree ROS2 环境后 build：

  ```bash
  cd ros2/A2
  colcon build --packages-select a2_lowlevel --cmake-args -DBUILD_TESTING=OFF
  ```
- [ ] 验证 `unitree_hg` ROS2 generated message：

  - `unitree_hg/msg/LowCmd`
  - `unitree_hg/msg/LowState`
  - `unitree_hg/msg/MotorCmd`
  - 字段 `mode_pr`、`mode_machine`、`motor_cmd[35]`、`reserve[4]`、`crc` 是否和当前代码一致。
- [ ] 用 Unitree SDK2 sample 或实机 low-level smoke 对照 A2 CRC；如不一致，修正 `a2_crc` raw layout。
- [ ] 首次实机前增加或确认安全流程：

  - 关闭 `ai_sport` / `ai_sports`。
  - 离地或限功率 smoke。
  - 准备 hardware emergency stop。
- [ ] 后续接 policy 前定义 A2 policy contract：

  - observation layout
  - action dimension
  - joint order
  - action scaling / PD gains
  - `ManagerBasedEnv` adapter 边界
