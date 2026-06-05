# TODO

## 2026-06-04 14:34 HKT

- [ ] 在 A2 部署机运行：

  ```bash
  # 在 AliengoSim2Real repo root 执行
  PROJECT_ROOT="$(pwd)"
  UNITREE_ROOT="$(cd .. && pwd)/third_party/unitree"
  mkdir -p "$UNITREE_ROOT"

  git clone https://github.com/unitreerobotics/unitree_ros2 "$UNITREE_ROOT/unitree_ros2"
  git clone https://github.com/unitreerobotics/unitree_sdk2 "$UNITREE_ROOT/unitree_sdk2"
  git clone https://github.com/unitreerobotics/unitree_sdk2_python "$UNITREE_ROOT/unitree_sdk2_python"
  # 安装 Unitree 官方 SDK

  bash "$PROJECT_ROOT/ros2/A2/scripts/collect_deploy_machine_info.sh" --unitree-root "$UNITREE_ROOT" --ping > "$PROJECT_ROOT/DeployMachineINFO.md"
  ```
  将 `DeployMachineINFO.md` 回传给 Codex，用于调整 A2 deployment chain。
- [ ] 在部署机确认 `AliengoSim2Real` 同级 parent `projects` 下的 `third_party/unitree` 存在：

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
- [ ] 在部署机安装/配置 LibTorch + jsoncpp 后 build A2 policy deploy：

  ```bash
  cd ros2/A2
  colcon build --packages-select a2_lowlevel --cmake-args \
    -DBUILD_TESTING=OFF \
    -DBUILD_A2_POLICY_DEPLOY=ON
  ```

## 2026-06-05 16:52 HKT

- [ ] 在部署机/实机验证 A2 R3 remote layout 和 safety gate：

  - `a2_lowlevel_smoke --ros-args -p log_remote:=true` 能随 stick/button 变化打印正确 `lx/rx/ry/ly` 和 button names。
  - `a2_policy_deploy --ros-args -p command_source:=remote` 中 `L2` release 强制 policy command `[0,0,0]`。
  - `L2` held 时方向符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
  - `Select` 和 `L2+B` 能触发 local stop、`publish_zero()`，并要求 history 重新 warm。
