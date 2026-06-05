# TODO

## 2026-06-04 14:34 HKT

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

## 2026-06-05 18:14 HKT

- [ ] 按 `ros2/A2/scripts/A2_DOCKER_BUILD_TEST.md` 在部署机执行 Docker build/preflight/offline smoke，并回填实际 pass/fail。
- [ ] 在 A2 部署机 build Docker image：

  ```bash
  cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
  bash ros2/A2/docker/build_image.sh
  ```
- [ ] 在部署机运行 Docker preflight，不自动修改 host network：

  ```bash
  cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
  bash ros2/A2/docker/preflight.sh --iface enp131s0 --container-check
  ```
- [ ] 手动配置 A2 low-level subnet，并确认 `192.168.123.x` 连通；`192.168.124.x` 不是当前 SDK2 low-level DDS chain 使用的 subnet：

  ```bash
  sudo ip link set enp131s0 up
  sudo ip addr flush dev enp131s0
  sudo ip addr add 192.168.123.99/24 dev enp131s0
  bash ros2/A2/docker/preflight.sh --iface enp131s0 --ping
  ```
- [ ] 在 Docker container 内 build low-level adapter：

  ```bash
  A2_NET_IFACE=lo bash ros2/A2/docker/run_container.sh bash
  /opt/a2/build_a2_workspace.sh --lowlevel-only --cmake-release
  ```
- [ ] 在 Docker container 内 build A2 policy deploy：

  ```bash
  /opt/a2/build_a2_workspace.sh --policy --cmake-release
  ```

## 2026-06-05 16:52 HKT

- [ ] 在部署机/实机验证 A2 R3 remote layout 和 safety gate：

  - `a2_lowlevel_smoke --ros-args -p log_remote:=true` 能随 stick/button 变化打印正确 `lx/rx/ry/ly` 和 button names。
  - `a2_policy_deploy --ros-args -p command_source:=remote` 中 `L2` release 强制 policy command `[0,0,0]`。
  - `L2` held 时方向符合 `ly -> vx`、`-lx -> vy`、`-rx -> yaw`。
  - `Select` 和 `L2+B` 能触发 local stop、`publish_zero()`，并要求 history 重新 warm。
