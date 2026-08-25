# A2 Memory

Updated: 2026-08-24 21:36 HKT

本目录记录 `ros2/A2` A2 专属部署链路的可复用项目事实、当前施工状态、blocker 和下一步 TODO。稳定技术概念保留 English technical terms。

2026-08-24在m45/domain 0实测：`/lowcmd`的`_CREATED_BY_BARE_DDS_APP_` publisher以约1000 Hz持续发送；`MotionSwitcherClient::CheckMode()`同时返回`form='0', name='ai', service='ai_sport'`。操作员明确授权后，既有guarded wrapper执行`ReleaseMode ret=0`，随后mode为空且5秒`/lowcmd`计数为0，确认该流量来自宇树官方`ai_sport`控制链，正确交接方式是MotionSwitcher release而非kill进程。Evidence：`deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/a2-motion-release/20260824_213519`。Stage2测试期间曾保持released；测试结束必须在自研publisher停止并`no-lowcmd` PASS后SelectMode恢复`ai_sport`，再以`service='ai_sport'`验收。

2026-08-25 00:51 HKT最终Stage2 dual live成功结束后，formal stop先完成A2 zero-LowCmd，随后`restore-a2`实测`no-lowcmd_count=0`、`SelectMode('ai_sport') ret=0`并最终`CheckMode form='0' name='ai' service='ai_sport'`。当前A2已恢复宇树官方mode，不再是released状态。Evidence：`deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/a2-restore/20260825_005440_131866`。下次Stage2 live仍由runner在启动direct node前执行`motion-check→motion-release`。

同机当前静止趴地姿态已只读采集5秒/5263样本，training order为`[0.3602,-0.3789,0.3382,-0.3506,1.1862,1.1942,1.2177,1.1831,-2.7570,-2.7380,-2.7485,-2.7468] rad`，各轴range不超过`0.0001 rad`。该目标仅用于Stage2 second-stop prone interpolation，不替换main A2默认controlled-down。Evidence：`deploy/a2_piper_stage2/.stage2_sessions/20260824_173406/evidence/pre-enable-stop-lifecycle/20260824_214754`。

## Entries

- `memory/a2_deploy_progress/`
  - 当前 A2 ROS2 low-level deployment 进度。
  - 覆盖 `a2_lowlevel` package、A2 low-level interface、A2 policy deploy、Docker deployment layer、formal `linux/amd64` deploy platform、Mac Docker Desktop offline validation skip decision、smoke node、deploy machine info collector、部署机 Docker build/test guide、real robot validation guide/scripts、joint state mapping/direction observe-only validation、`joints-live` / `remote-live` live observation tools、connected preflight topic/type result、`no-lowcmd` observe-only safety check、remote local stop `enable_motion` safety boundary、部署机验证 TODO。

## Routing

- A2 标准版 ROS2 low-level control、A2 policy deploy、A2 Docker deployment/platform、Docker build/test guide、real robot validation guide/scripts、joint state mapping/direction observe-only validation、`joints-live` / `remote-live` live observation tools、Mac offline Docker validation decision、`unitree_hg`、ROS2 visible `/lowstate`/`/lowcmd`、configured topic type checks、`no-lowcmd` observe-only safety check、remote local stop `enable_motion` safety boundary、official DDS `rt/lowstate`/`rt/lowcmd`、A2 deploy machine readiness，先读 `memory/a2_deploy_progress/description.md`。
- 需要判断当前 blocker 或下一步施工时，再读同 entry 的 `TODO.md` 和 `DONE.md`。
- A2 SDK/reference docs 只引用 `../A2_Guide/`，不要复制长文档到 memory。
- Go2W 现有链路仍在 `../src/**`，对应 memory 是 `../src/memory/MEMORY.md`；不要把 A2 memory 当作 Go2W runtime 事实来源。
- 全局 routing 入口是 `../../MEMORY.md`。
