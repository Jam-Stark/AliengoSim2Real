# Stage2 deployment open items

以下事项没有被 policy bundle 或本地离线 runtime验证，不得用常见参数代填。

## A2 direct path现场项

- External named A2 semantic endpoint已不再是blocker。C++ direct node复用main路径：raw `/lowstate`/`/lowcmd`、training→raw mapping `[3,0,9,6,4,1,10,7,5,2,11,8]`、raw quaternion `wxyz`、PD `140/5`与`220/9`、唯一`publish_joint_commands()` boundary。
- 在同一台A2的read-only/dry-run Gate核对projected gravity、joint order/sign、receipt-time age/skew；raw LowState没有Header，所以当前同步语义明确是local steady receipt time，不声称source-clock同步。
- 逐关节确认方向、零位、hardware position/rate limits与normal/software stop；这些现场结果不能由repository mapping代替。
- 受控确认direct process/network故障后的A2行为；existing fresh-state check只拒绝新的publish，不自动等价于现场认可的hold/stop。

## PiPER / PC2 hardware

- 采集 PC2 OS、ROS、PiPER SDK revision、USB-CAN枚举、`can_piper` bitrate/interface、firmware/API status与真实 launch command。
- 在 PC2 上完成 `/piper/joint_states` rate/jitter与 diagnostics只读检查，再验证 enable -> fresh command -> stop -> explicit resume、command timeout和 feedback-loss paths。
- 将 LMP URDF arm limits与 bridge/manufacturer/site limits取交集；确认 j1-j6方向、零位与实际 soft/hard limits。
- 继续保持 gripper absent；只有独立接口、策略 contract与硬件验证都完成后才能扩展。

## Site contract

- 复制`config/site.template.yaml`为`config/site.yaml`；template已填同一A2可证的raw topics、mapping、quaternion extraction、PD、age/skew与PiPER interface/timeouts。
- 仍须填写hardware joint/rate limits、deadline miss decision、initial range、hold/stop/recovery与physical E-stop；static arm goal`[0.6,0,0]`只用于first bring-up。
- 保持`safety.output_enabled: false`直到所有site字段有evidence。Live还要求component approval receipt、`STAGE2_ALLOW_LIVE=1`和显式`--live`；Gate script不修改flag或调用PiPER resume。

## Deployment host verification

- 在最终 Ubuntu 22.04 / ROS 2 Humble container上安装 CPU Torch candidate并重新执行 bundle parity与 arm+dog benchmark。export host的 Python/Torch/CUDA版本不是目标 host pass。
- 明确 ROS 2 Humble Python与policy Python的共存方式；不得只用模型可 `torch.jit.load()` 代替完整 parity。
- 保留 action parity的 pre-limit语义：先核对 `default + 0.25 * raw` reference，再核对 LMP-limit、site-limit与rate-limit后的实际 named target。
- Repository已提供可执行read-only、dry-run、process-stop、10分钟shadow和component live Gate；它们的site receipts尚未在真实硬件生成，因此仍不能标记为已完成。
