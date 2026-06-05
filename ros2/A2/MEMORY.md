# A2 Memory

Updated: 2026-06-05 22:03 HKT

本目录记录 `ros2/A2` A2 专属部署链路的可复用项目事实、当前施工状态、blocker 和下一步 TODO。稳定技术概念保留 English technical terms。

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
