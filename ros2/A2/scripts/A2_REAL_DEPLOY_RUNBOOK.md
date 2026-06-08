# A2 Docker Real Deployment Runbook

本文档面向 day-to-day real A2 operator。它不是 validation guide；目标是在部署机冷启动后，
按固定顺序完成 Docker image/container、A2 network、ROS2 workspace build/source、
MotionSwitcher mode switch、policy enable、two-A handover、stop/restore/disconnect。

部署机 workspace 固定为：

```text
/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
```

官方 A2 low-level DDS reference topics 是 `rt/lowstate` / `rt/lowcmd`。当前本仓库在部署机
ROS2 graph 中默认使用 visible topics `/lowstate` / `/lowcmd`。

## 0. Safety and Operator Roles

`policy-enable-remote` 是 true motion path。first `A` 后会发布 stand-up / hold LowCmd，
second `A` handover 后会进入 policy warmup / active path。不要把该流程当 listen-only test。

硬性安全边界：

- 准备 hardware emergency stop，并确认 remote/e-stop operator 手能立即触达。
- 现场只允许一个 configured LowCmd publisher。默认 LowCmd topic 是 `/lowcmd`。
- 进入任何 publish path 前必须先运行 `no-lowcmd 5` 并确认 pass。
- low-level control 前关闭 Unitree built-in motion service；结束后恢复前也必须先停止
  policy/LowCmd publisher 并重新 `no-lowcmd 5` pass。
- `Select` 是 primary local stop。`L2+B` 只作为 additional stop path；只有 `L2` decode 正常时才可靠。
- `B` 只用于 stand-up / hold / warmup phase 的 cancel。
- `stand_test` 不用于 real deployment runbook。

Operator roles 可以由两个人分担：terminal operator 执行命令，remote/e-stop operator 盯机器人、
遥控器和 hardware emergency stop。只有在场地、姿态、线缆和急停位置允许时，同一人才能同时承担两种角色。

## 1. Host Cold Start

Host terminal:

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
pwd
docker --version
docker info >/dev/null
echo "docker-ok"
```

设置并打印 formal image / platform：

```bash
export A2_DOCKER_IMAGE=a2-humble-deploy:2026-06-05
export A2_DOCKER_PLATFORM=linux/amd64
printf 'A2_DOCKER_IMAGE=%s\nA2_DOCKER_PLATFORM=%s\n' \
  "$A2_DOCKER_IMAGE" "$A2_DOCKER_PLATFORM"
```

可选检查 image 是否存在；缺失时 build：

```bash
docker image inspect "$A2_DOCKER_IMAGE" >/dev/null || \
  bash ros2/A2/docker/build_image.sh
```

Ideal result:

- `pwd` 是 `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`。
- Docker commands 无 error。
- image 是 `a2-humble-deploy:2026-06-05`。
- platform 是 `linux/amd64`。

## 2. Connect A2 Network

Host terminal，确认 A2 Ethernet cable 已连接到 `enp131s0` 后执行：

```bash
sudo ip link set enp131s0 up
sudo ip addr flush dev enp131s0
sudo ip addr add 192.168.123.99/24 dev enp131s0
ip -4 addr show enp131s0
ping -c 5 192.168.123.161
```

Ideal result:

- `ip -4 addr show enp131s0` 显示 `192.168.123.99/24`。
- `ping -c 5 192.168.123.161` 有 reply。

不要把 `192.168.124.x` 用作当前 SDK low-level DDS chain。本链路按 `192.168.123.x`
连接 A2 low-level network。

## 3. Enter Docker

Host terminal:

```bash
cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real
A2_NET_IFACE=enp131s0 bash ros2/A2/docker/run_container.sh bash
```

Container terminal:

```bash
pwd
echo "ROS_DISTRO=${ROS_DISTRO}"
echo "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "Torch_DIR=${Torch_DIR}"
printf '%s\n' "$CYCLONEDDS_URI"
```

Ideal result:

- `pwd` 是 `/work/projects/AliengoSim2Real/ros2`。
- `ROS_DISTRO=humble`。
- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`。
- `Torch_DIR=/opt/libtorch/share/cmake/Torch`。
- `CYCLONEDDS_URI` 中包含 `NetworkInterface name="enp131s0"`。

## 4. Build/Source Workspace

Container terminal:

```bash
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 pkg prefix a2_lowlevel
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_lowlevel_smoke
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_policy_deploy
```

If missing or stale:

```bash
/opt/a2/build_a2_workspace.sh --policy --cmake-release
source /work/projects/AliengoSim2Real/ros2/install/setup.bash
ros2 pkg prefix a2_lowlevel
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_lowlevel_smoke
test -x /work/projects/AliengoSim2Real/ros2/install/a2_lowlevel/lib/a2_lowlevel/a2_policy_deploy
```

Do not run `colcon` directly during normal deployment unless this helper fails and you are debugging build state.

## 5. Connected Readiness

Container terminal:

```bash
A2/scripts/a2_real_robot_test.sh connected-preflight enp131s0
A2/scripts/a2_real_robot_test.sh no-lowcmd 5
A2/scripts/a2_real_robot_test.sh lowstate 15
```

Optional remote live observation:

```bash
A2/scripts/a2_real_robot_test.sh remote-live 30
```

`remote-live` is observe-only; use Ctrl-C to stop early if running with duration `0`.

Ideal result:

- `connected-preflight` sees configured `/lowstate` and `/lowcmd` with `unitree_hg` types.
- `/lowcmd` endpoint visibility is not treated as active command traffic.
- `no-lowcmd 5` prints `PASS: no /lowcmd messages observed`.
- `lowstate 15` prints a fresh/rate/finiteness pass.
- `remote-live 30`, if used, shows valid sticks/buttons and does not publish LowCmd.

If any readiness step fails, stop here and collect logs from `/tmp/a2_real_robot_tests`.

## 6. Switch Motion Service to Low-Level

Container terminal, first check current built-in mode:

```bash
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

If `CheckMode` shows a non-empty `name`, release Unitree built-in motion service:

```bash
A2_ALLOW_RELEASE_MODE=1 A2/scripts/a2_real_robot_test.sh motion-release enp131s0
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

Ideal result:

- `motion-check` compiles/runs the SDK2 MotionSwitcher helper and prints `CheckMode`.
- After release, `motion-check` confirms `name=''`.

MotionSwitcher supports `CheckMode`, `SelectMode`, and `ReleaseMode`. This runbook uses
`CheckMode` before/after, guarded `ReleaseMode` for low-level control, and guarded
`SelectMode` during restore. If MotionSwitcher RPC is unavailable, use Unitree App fallback
to close built-in motion service, then re-run `motion-check` and confirm `name=''`.

## 7. Policy Listen-Only Gate

Container terminal:

```bash
A2/scripts/a2_real_robot_test.sh policy-listen-remote 20
```

Confirmation:

- policy logs show `enable_motion=false` and `command_source=remote`.
- no-lowcmd observer covers the policy runtime.
- final observer output is `PASS: no /lowcmd messages observed`.

If any `/lowcmd` message is observed in this stage, do not continue to real policy.

Optional force-estimator auxiliary monitor, still listen-only and independent of active motion:

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-live 30
```

For continuous live output, use duration `0` and stop with Ctrl-C:

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-live 0
```

Ideal result:

- `policy-aux-live` prints action dim and aux dim after history warm.
- If aux dim is `6`, logs show `pred_base_lin_vel` and `pred_base_force_local`.
- The paired no-lowcmd observer still prints no configured `/lowcmd` messages.
- Brake gate params are passed but cannot publish LowCmd in this `enable_motion=false`
  listen-only path.

## 8. Start Real Policy

This is the first day-to-day deployment motion command. Confirm robot support/clearance,
hardware emergency stop, one LowCmd publisher, released built-in motion mode, and centered sticks.

先检查 run config。默认路径是
`/work/projects/AliengoSim2Real/ros2/A2/config/a2_policy_remote.env`；如现场需要临时
覆盖速度上限、deadzone 或 aux monitor 参数，复制/编辑一个 `.env`，然后通过
`A2_POLICY_RUN_CONFIG=/path/to/file.env` 指定。Operator shell 中已有的 `A2_POLICY_*`
env 会覆盖 config 文件值。

```bash
sed -n '1,120p' A2/config/a2_policy_remote.env
```

确认至少包含：

```text
A2_POLICY_MAX_REMOTE_VX=0.80
A2_POLICY_MAX_REMOTE_VY=0.50
A2_POLICY_MAX_REMOTE_YAW=0.6
A2_POLICY_REMOTE_DEADZONE=0.08
A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY=true
A2_POLICY_PUBLISH_AUX_DEBUG=true
A2_POLICY_AUX_DEBUG_TOPIC=/a2/policy_aux
A2_POLICY_AUX_EXPECTED_DIM=6
A2_POLICY_AUX_PRINT_PERIOD=0.2
A2_POLICY_STANDING_WALKING_GATE_ENABLED=true
A2_POLICY_STANDING_WALKING_ENTER_FORCE_XY_THRESHOLD=0.2
A2_POLICY_STANDING_WALKING_EXIT_FORCE_XY_THRESHOLD=0.05
A2_POLICY_BRAKE_GATE_ENABLED=true
A2_POLICY_BRAKE_FORCE_X_THRESHOLD=-0.6
A2_POLICY_BRAKE_MIN_CMD_VX=0.2
A2_POLICY_BRAKE_MAX_ABS_YAW=0.10
A2_POLICY_BRAKE_HOLD_STEPS=2
```

不要把 `A2_ALLOW_ENABLE_MOTION=1` 写进 run config；它必须只在执行真运动命令的
operator shell 里显式设置。
Standing/walking gate is active by default and only controls the policy gait clock.
It does not modify the raw requested command, policy action, LowCmd, or
`publish_joint_commands()` path. When raw requested command is not standing, gait
mode is `command_walking`. When raw requested command is standing, it uses aux dim 6
`pred_base_force_local[0:2]`: `force_xy=hypot(aux[3], aux[4])`, with hysteresis
enter `0.2` and exit `0.05`. Values between `0.05` and `0.2` keep the current
standing/force_walking mode. Aux dim < 6 or NaN/Inf does not enter `force_walking`;
if already `force_walking`, it returns to standing. Aux is only available after
policy inference, so force-derived mode changes affect the next observation.

Brake gate is active in the real motion path. It uses A2 observed unitless aux
scale, not Newton: default trigger is `pred_base_force_local[0] <= -0.6` for 2
consecutive control steps while `cmd_vx >= 0.2`, `abs(cmd_yaw) <= 0.10`, and the
command is not standing. Current tick does not publish zero LowCmd, does not
switch stop mode, does not clear PD, and does not skip the policy joint command;
the already-computed action continues through normal `publish_joint_commands()`.
From the next observation, brake active overrides only the policy observation
command to `[0, 0, 0]` and freezes gait clock at the standing phase. Centering
the stick / command standing, losing eligibility, local stop, or runtime reset
releases the latch.
Brake active has priority over standing/walking gate and always forces gait clock
standing freeze.

Use two Docker terminals if you want live force-estimator aux while the active policy runs.

Terminal 1, active policy motion path:

```bash
A2_ALLOW_ENABLE_MOTION=1 A2/scripts/a2_real_robot_test.sh policy-enable-remote 120
```

Terminal 2, aux topic subscriber only:

```bash
A2/scripts/a2_real_robot_test.sh policy-aux-monitor 0
```

`policy-aux-monitor` only subscribes to `A2_POLICY_AUX_DEBUG_TOPIC` (default `/a2/policy_aux`).
It does not start a policy node, does not start no-lowcmd observer, and never publishes LowCmd.
It can be started before or after Terminal 1; it will not print samples until the active
`a2_policy_deploy` instance has warmed history and completed policy inference.
Use it to observe `pred_base_force_local[0]` before/after brake gate events and verify
the `-0.6` sign/threshold margin; it does not control the brake gate.

Optional topic check from either container terminal:

```bash
ros2 topic info /a2/policy_aux -v
```

Expected aux topic check:

- type is `std_msgs/msg/Float32MultiArray`.
- publisher is the active `a2_policy_deploy` node from Terminal 1.
- Terminal 2 monitor is subscription-only.
- no aux samples before history warmup is normal; no aux samples after policy inference means
  check `A2_POLICY_PUBLISH_AUX_DEBUG`, `A2_POLICY_AUX_DEBUG_TOPIC`, and
  `policy_enable_remote_*.log`.

Remote handover sequence:

- first `A` starts stand-up interpolation.
- holder keeps policy default pose after stand-up.
- second `A` starts warmup/handover.
- second `A` requires `lx/rx/ly` centered after deadzone; otherwise holder continues.
- after warmup/history/action validation, the next valid cycle enters `PolicyActive`.

PolicyActive stick mapping:

- `ly -> vx`
- `-lx -> vy`
- `-rx -> yaw`
- `L2` is not a locomotion gate.

Default run config speed caps:

```text
max_remote_vx=0.80
max_remote_vy=0.50
max_remote_yaw=0.6
remote_deadzone=0.08
require_standup_before_policy=true
publish_aux_debug=true
aux_debug_topic=/a2/policy_aux
standing_walking_gate_enabled=true
standing_walking_enter_force_xy_threshold=0.2
standing_walking_exit_force_xy_threshold=0.05
brake_gate_enabled=true
brake_force_x_threshold=-0.6
brake_min_cmd_vx=0.2
brake_max_abs_yaw=0.10
brake_hold_steps=2
```

Any abnormal behavior: release sticks, press `Select`, then use hardware emergency stop if the robot
does not immediately become safe.

## 9. Runtime Stop

Primary local stop:

- Press `Select`.
- Policy node resets runtime and requires a new two-A handover before motion resumes.
- In `enable_motion=true`, local stop publishes zero LowCmd.

Additional stop/cancel paths:

- `B` cancels stand-up / hold / warmup phases.
- `L2+B` is additional local stop only if `L2` decodes correctly.
- hardware emergency stop is mandatory for abnormal behavior.
- Ctrl-C stops the process in terminal, but do not use Ctrl-C as the only safety mechanism.

After stopping the policy process, verify no active LowCmd traffic:

```bash
A2/scripts/a2_real_robot_test.sh no-lowcmd 5
```

Only continue after it prints `PASS: no /lowcmd messages observed`.

## 10. Restore and Disconnect

Container terminal:

```bash
A2/scripts/a2_real_robot_test.sh no-lowcmd 5
A2_ALLOW_SELECT_MODE=1 A2/scripts/a2_real_robot_test.sh motion-restore enp131s0
A2/scripts/a2_real_robot_test.sh motion-check enp131s0
```

Ideal result:

- `no-lowcmd 5` pass.
- `motion-restore` prints `SelectMode('ai_sport') ret=0`.
- following `motion-check` confirms raw `name='ai'` with normalized `service='ai_sport'`
  for `form='0'`, or raw `name='ai_sport'`. The raw `name='ai'` / `service='ai_sport'`
  pair is the expected Unitree SDK2 alias for `ai_sport`.

If restore fails:

Use Unitree App fallback and then run `motion-check enp131s0` again. If the operator explicitly
needs to probe a site-specific `ai_sports` MotionSwitcher string from the basic-service guide, run it
as a manual diagnostic only and keep the full log; the helper does not treat `ai_sports` as an
automatic alias for canonical `ai_sport`.

After restore is confirmed:

```bash
exit
```

Then disconnect the A2 Ethernet cable from the deploy machine.

## 11. Logs and Failure Collection

Logs are written under:

```bash
ls -l /tmp/a2_real_robot_tests
```

Expected log families:

- `connected_preflight_*.log`
- `no_lowcmd_*.log`
- `lowstate_*.log`
- `remote_live_*.log`
- `motion_check_*.log`
- `motion_release_*.log`
- `policy_listen_remote_*.log`
- `policy_listen_no_lowcmd_*.log`
- `policy_aux_live_*.log`
- `policy_aux_live_no_lowcmd_*.log`
- `policy_aux_monitor_*.log`
- `policy_enable_remote_*.log`
- `motion_select_*.log`

Compact failure collection from the container:

```bash
cd /work/projects/AliengoSim2Real/ros2
git rev-parse --short HEAD
env | grep -E '^(ROS_DISTRO|RMW_IMPLEMENTATION|A2_NET_IFACE|A2_LOWSTATE_TOPIC|A2_LOWCMD_TOPIC|CYCLONEDDS_URI|Torch_DIR)='
ip addr show enp131s0
ping -c 5 192.168.123.161
ros2 topic list
ros2 topic info /lowstate -v || true
ros2 topic info /lf/lowstate -v || true
ros2 topic info /lowcmd -v || true
ros2 topic info /a2/policy_aux -v || true
find /tmp/a2_real_robot_tests -maxdepth 1 -type f -name '*.log' -print
```

When reporting a failure, include which section failed, the exact command, whether hardware emergency
stop was used, and the log files above.
