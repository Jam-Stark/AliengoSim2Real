# A2 + PiPER Stage2 dual-policy deployment

本目录接入真实 LMP Stage2 导出，继续复用仓库现有的 A2 low-level 与 PC2 PiPER bridge，不复制两套底层控制实现。第一次部署或日常 bring-up 请只从 [新手操作员 Runbook](docs/operator_runbook_zh_CN.md) 开始；它给出每台机器、每个终端、可复制命令、精确 PASS、停止条件和 evidence 路径。

当前正式 policy host 是 `baoquanc@ai-precog-m45`；已采集的 OS/GPU/NIC、安装状态与精确路径见 [m45 policy host 实机档案](docs/policy_host_m45.md)。

## 当前可用范围

- `policy_bundle/`：用户提供的原始导出包，保持目录和内容不变。
- `ros2/a2_piper_stage2_direct/`：正式实机路径；与 main 成功案例相同，在 C++ node 内组合 `A2LowLevelInterface`，并订阅 PC2 `/piper/joint_states`。
- `src/a2_piper_stage2_deploy/`：offline parity/benchmark/mock runtime；Python external-semantic ROS live 不是正式实机路径。
- `config/site.mock.yaml`：只用于离线 mock/shadow。
- `config/site.template.yaml`：同一台 A2 的 direct-mode 现场模板，已填 raw topic、mapping、quaternion extraction、PD和PiPER bridge contract；现场 limits/operations仍为 `TO_VERIFY`。
- `config/stage2_direct.params.yaml`：C++ direct node安全默认参数；`enable_motion=false`、`live_acknowledged=false`。
- `scripts/stage2_gate.sh`：有顺序的 Gate runner，receipts 写入 `.stage2_sessions/<id>/`。
- `docker/`、`scripts/`：Ubuntu/Docker bootstrap、CPU-only image、read-only probes、offline verification和direct runtime入口。

A2 的 semantic信息、training mapping和成功 LowCmd控制路径并不缺失；缺的是旧“Python external policy host只使用标准named ROS消息”架构需要的 endpoint。本部署已经选择 direct mode，因此不新增 A2 semantic bridge：正式 C++ node只通过现有 `publish_joint_commands()` 进入 `/lowcmd`，继续保留fresh-state、mode与CRC边界。PiPER仍只通过PC2 semantic bridge，PC2是CAN唯一owner。

## 已填入的真实 Stage2 contract

- dog actor：`[B, 1620] -> [B, 12]`，`54 × 30` history。
- arm actor：`[B, 600] -> [B, 8]`，`20 × 30` history；`[0:6]` 是 arm control，`[6:8]` 是模型内 `tanh` 后的 body pitch/roll plan。
- policy period：`0.005 s × 4 = 0.02 s`，即 `50 Hz`。
- observation：frame-major、oldest-to-newest、无 normalization/clip/corruption。
- action：`position_offset`，dog/arm scale 均为 `0.25 rad`；joint order、default position、LMP URDF limits 和 training-derived per-tick rate limit均来自 bundle manifest。
- runner role：`training_state`，不是第三个 actor。
- gripper：无 actor output；arm plan 不能映射成 gripper command。

`parity/*_reference.npz` 中的 `final_joint_target_rad` 是 `default + 0.25 × raw` 再做 `[-100, 100]` clip 的 nominal policy target，尚未应用现场 hard limit 或 per-tick rate limit。部署 runtime 将 nominal target 与 limited target 分层处理；二者不能直接做相等 parity。

## 唯一 operator 入口

```bash
cd deploy/a2_piper_stage2
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only
./scripts/stage2_gate.sh init --operator <name>
./scripts/stage2_gate.sh offline
./scripts/stage2_gate.sh next
```

完整顺序见 [operator_runbook_zh_CN.md](docs/operator_runbook_zh_CN.md)。不要从 README 直接拼 live command。

## 单独 Offline 入口

```bash
cd deploy/a2_piper_stage2
./scripts/configure_policy_host.sh \
  --iface enp130s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.mock.yaml"
./scripts/check_policy_host.sh
./scripts/build_container.sh
./scripts/run_shadow.sh mock
```

也可在已经安装 CPU Torch 2.7.0 的 Python 环境直接运行：

```bash
python -m pip install -e .
python -m a2_piper_stage2_deploy.cli validate --bundle policy_bundle
python -m a2_piper_stage2_deploy.cli benchmark --bundle policy_bundle
python -m a2_piper_stage2_deploy.cli mock-shadow --bundle policy_bundle
```

首次 live 之前必须完成 [bringup gates](docs/bringup_gates.md) 与对应 session receipts。Live同时要求人工approval、site `output_enabled: true`、`STAGE2_ALLOW_LIVE=1`和显式 `--live`；Gate runner不会修改site或调用PiPER resume。Bundle parity与sim2sim效果都不等于hardware pass。
