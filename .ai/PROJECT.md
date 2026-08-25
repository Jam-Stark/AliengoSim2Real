# AliengoSim2Real project overlay

## Identity

- Repository: `Jam-Stark/GeneralSim2Real`（本地目录名 `AliengoSim2Real`）
- Primary branch/worktree: 当前工作分支 `codex/a2-piper-lmp-stage2-deploy-20260823`
- Domain: robot policy deployment、MuJoCo simulation、ROS1/ROS2、A2 + PiPER dual-policy runtime
- Source-truth order: actual source/config/runtime artifact > matching subsystem memory > README/runbook > plans

## Real paths

```text
entrypoints:
  ROS1 Aliengo: ros1/src/, ros1/launch/, ros1/scripts/
  ROS2 Go2W: ros2/src/
  ROS2 A2: ros2/A2/
  ROS2 PiPER: ros2/Piper/
  A2 + PiPER Stage2: deploy/a2_piper_stage2/
configs:
  package/build: ros1/CMakeLists.txt, ros1/package.xml, ros2/**/CMakeLists.txt, ros2/**/package.xml
  Stage2 site/runtime: deploy/a2_piper_stage2/config/, deploy/a2_piper_stage2/docker/
runtime/evaluation:
  shared policy runtime: utils/cpp_manager_env/
  simulation: mujoco/
  deployment checks and runbooks: each subsystem's scripts/ and docs/
artifacts:
  policy assets: policy/
  generated ROS build/install/log and experiment outputs remain local unless explicitly requested
memory routes:
  router: MEMORY.md
  global: memory/MEMORY.md
  ROS1: ros1/memory/MEMORY.md
  Go2W: ros2/src/memory/MEMORY.md
  A2: ros2/A2/MEMORY.md
  PiPER: ros2/Piper/MEMORY.md
  Stage2: deploy/a2_piper_stage2/MEMORY.md
```

## Protected paths

- `MEMORY.md`、`memory/**` 与各 subsystem `MEMORY.md`/memory entries；
- `.codex/config.toml` 与 `.codex/agents/**`（如后续创建）；
- `policy/**` 中的 model assets 与 metadata；
- robot/runtime source、site config、deployment receipts 和 hardware runbooks。

Jam Coding Role 的 `refresh` 只可更新带 managed marker 的 `.ai` core；project overlay、runtime config、memory 和本地配置保持 project-owned。

## Validation map

- Python/tooling syntax: 对本次修改的脚本执行 `python3 -m py_compile` 或对应 CLI audit。
- ROS1: 在匹配 ROS Noetic/catkin 环境中构建目标 package。
- ROS2: 在匹配 ROS 2 Humble、Unitree SDK 与 package dependency 环境中运行 `colcon build --packages-select <package>`。
- MuJoCo/policy runtime: 使用对应 CMake/runtime command 与真实 policy/config 验证；静态检查不等同于 runtime pass。
- Simulation、experiment 与 hardware 结论分别记录；实机结论只能来自指定 hardware gate/runbook 的现场 evidence。

## Resource and safety boundaries

- GPU、Isaac Sim/display、ROS domain/port、Unitree network interface、PiPER CAN device 与 output directory 是按任务分配的 exclusive resources。
- Git commit/push、外部上传、昂贵长跑与实机动作需要当前任务的明确授权。
- 超过 30 分钟的训练/评估放入独立 tmux session，并记录 command、cwd、environment、output 与 stop condition。
- 实机执行前遵守对应 subsystem runbook，核对 joint order、PD gains、action scaling、controller service ownership、watchdog、急停与人员隔离；异常时停止，不用 fallback 强行继续。
