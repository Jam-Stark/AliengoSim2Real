# DONE

## 2026-08-24 HKT

- [x] 在 PC2 bridge 基线分支建立独立 `deploy/a2_piper_stage2/`。
- [x] 导入真实 Stage2 dog/arm TorchScript、manifest、metadata 和 parity references。
- [x] 实现 manifest-driven arm-first runtime、30-frame history、dog same-tick preview、raw last-action cache、named action processing、CPU parity/benchmark/mock CLI。
- [x] 分离 nominal policy target 与 LMP-URDF/rate-limited target语义。
- [x] 确认当前分支复用 main 已成功 A2 locomotion 路径，新增 C++ `a2_piper_stage2_direct` 而不新增 A2 semantic bridge。
- [x] 实现 live site limit/rate contract、deadline miss、two-A handover、warmup limiter seed、PiPER semantic output 和分组件 mode。
- [x] 实现 Ubuntu/Docker bootstrap、CPU Torch/LibTorch 2.7 image、host/PC2/ROS probes、session Gate runner 和中文操作员 Runbook。
- [x] Gate runner加入只读`joint-observe`双路采集、逐关节人工表格与`joint-validation` approval前置；不在未知site limits时构造动作target。
- [x] 保留尚未现场验证的 limits/watchdog/stop/recovery 为 `TO_VERIFY`，不把代码存在写成实机 Gate 已通过。
