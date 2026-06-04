我之前训练的policy 里实际导出产物整理，结论如下。

Policy type:

- `AsymmetricActorCritic`，前馈式 adaptation policy，不是 RNN。
- `is_recurrent = False`，没有 LSTM/GRU/SRU，没有 hidden state。
- history encoder: `1472 -> [512,256,128] -> latent 64`
- actor: `(46 + 64) -> [512,256,128] -> 12`
- auxiliary estimator head: `64 -> [128,64] -> 6`，预测的是 `predObs = [base_lin_vel(3), base_forces_local(3)]`
- 实现细节：`policy_history_only=True`，模型把 flattened `policy` 向量最后 `46` 维切成 actor 的 “curr” 分支；这是当前代码事实。

Action dim:

- `12`
- raw policy 输出是 `a[0..11]`，无额外 arm/base 动作。
- 真正下发的是绝对关节位置 target：`q_target = q_default + 0.25 * a`
- 所以语义是：`12` 个“相对默认位姿的 delta”，经过 `scale=0.25` 后变成“绝对关节位置目标”
- 不是 velocity，不是 torque，不是混合动作。
- 训练里腿动作还带随机 delay：`0..1` 个 control step；以当前 `50 Hz` 控制算就是 `0..20 ms`。

Joint order in policy:

- `[0 FL_hip, 1 FR_hip, 2 RL_hip, 3 RR_hip, 4 FL_thigh, 5 FR_thigh, 6 RL_thigh, 7 RR_thigh, 8 FL_calf, 9 FR_calf, 10 RL_calf, 11 RR_calf]`

Aliengo motor order:

- 仓库里没有 Unitree SDK 的 `LowCmd.motorCmd[i]` 头文件；但 Aliengo URDF 的 transmission 枚举顺序明确是：
- `[0 FR_hip, 1 FR_thigh, 2 FR_calf, 3 FL_hip, 4 FL_thigh, 5 FL_calf, 6 RR_hip, 7 RR_thigh, 8 RR_calf, 9 RL_hip, 10 RL_thigh, 11 RL_calf]`
- 下面这张映射表我把它当作当前 repo 对齐的 “Aliengo motor order”；这也是常见 Unitree 12 电机顺序。这里我明确标注为“按 URDF transmission 顺序推断”。

Policy -> SDK mapping:

- `policy[0] FL_hip   -> sdk[3]`
- `policy[1] FR_hip   -> sdk[0]`
- `policy[2] RL_hip   -> sdk[9]`
- `policy[3] RR_hip   -> sdk[6]`
- `policy[4] FL_thigh -> sdk[4]`
- `policy[5] FR_thigh -> sdk[1]`
- `policy[6] RL_thigh -> sdk[10]`
- `policy[7] RR_thigh -> sdk[7]`
- `policy[8] FL_calf  -> sdk[5]`
- `policy[9] FR_calf  -> sdk[2]`
- `policy[10] RL_calf -> sdk[11]`
- `policy[11] RR_calf -> sdk[8]`

Observation items in order:

- 单帧 `46` 维，顺序固定为：
- `0:2   base_orientation = projected_gravity_b[x,y]`，维度 `2`
- `2:5   base_ang_vel_b * 0.25`，维度 `3`
- `5:17  joint_pos_rel = q - q_default`，维度 `12`
- `17:29 joint_vel_rel = qd - qd_default` 再乘 `0.05`，维度 `12`
- `29:41 last_action_raw`，维度 `12`
- `41:43 gait_clock = [sin(2πφ), cos(2πφ)]`，维度 `2`
- `43:46 commands = [vx, vy, wz] * [2.0, 2.0, 0.25]`，维度 `3`
- history 长度：`32`
- 最终 policy 输入总维度：`46 * 32 = 1472`
- 包含上一时刻 action：`是`
- 包含 command：`是`
- 当前 MuJoCo deploy/runtime 的 flatten 顺序是 term-major，且每个 term 内部是 oldest->newest：
- `[32x base_orientation][32x base_ang_vel][32x joint_pos_rel][32x joint_vel_rel][32x last_action][32x gait_clock][32x commands]`

Normalization/scales:

- 没有 running mean/var normalizer；`actor_obs_normalization=False`，`history_obs_normalization=False`
- IsaacLab observation 处理顺序是：`delay modifier -> noise -> clip -> scale`
- `base_orientation`: Gaussian noise `std=0.05`，无 scale
- `base_ang_vel`: Gaussian noise `std=0.2 rad/s`，scale `0.25`
- `joint_pos_rel`: Gaussian noise `std=0.01 rad`，scale `1.0`
- `joint_vel_rel`: Gaussian noise `std=1.5 rad/s`，scale `0.05`
- `last_action`: 直接用上一时刻 raw action，不是 `q_target`
- `clip` 全部基本是 `[-100, 100]`，对这些量几乎等于不裁
- `default_joint_pos = [0.1, -0.1, 0.1, -0.1, 0.5, 0.5, 0.5, 0.5, -1.0, -1.0, -1.0, -1.0]`
- `projected_gravity` 定义：`quat_apply(quat_inv(root_quat_w), [0,0,-1])`；policy 只取 `x,y`，不是 roll/pitch 欧拉角
- `root_quat_w` 如果你在实机侧自己算，应使用 world-frame quaternion，顺序 `(w,x,y,z)`
- `joint_pos` 是相对默认位姿；`joint_vel` 单位是 `rad/s`，相对默认速度（默认一般是 `0`）

Command definition:

- command 维度：`3`
- 语义：`[vx, vy, wz]`
- 单位：`vx, vy` 是 `m/s`，`wz` 是 `rad/s`
- 训练采样范围：`vx ∈ [-1.2, 1.2]`，`vy ∈ [-0.6, 0.6]`，`wz ∈ [-1.0, 1.0]`
- 小命令站立裁剪阈值：`|vx|<0.1 && |vy|<0.1 && |wz|<0.2` 时整体归零
- 坐标系是 body yaw frame / base-yaw local frame，不是 world frame
- 当前 repo 没有 joystick/wireless remote 到 command 的映射；现有部署脚本是命令行给定 `--locomotion-cmd VX VY WZ` 或 `--cmd-vx/--cmd-vy/--cmd-wz`

Visual input definition:

- 没有视觉输入；policy 不用 RGB，不用 depth，不订阅图像，不存在图像 history/resize/normalize。

Recurrent spec:

- 无 recurrent memory
- `num_layers / hidden_dim / reset hidden state`：`N/A`
- `student_info.json` / `student_deploy.json`：当前这条链路没有，用不上

Exported files:

- 当前 workspace 里可直接部署的一套是：
- [policy.pt](/home/baoquanc/workspace/ForceControl/logs/rsl_rl/uniFP_aliengo_position_force/2026-04-21_17-33-58_base_only/mujoco_exported/policy.pt)
- [policy.json](/home/baoquanc/workspace/ForceControl/logs/rsl_rl/uniFP_aliengo_position_force/2026-04-21_17-33-58_base_only/mujoco_exported/policy.json:1)
- TorchScript 接口：`forward(policy_obs_flat) -> (action[12], pred_est[6])`
- 这套 full export 没有 ONNX node 名；如果你需要 ONNX，仓库里确实有旧的 `exported/policy.onnx`，但那条 stock export 会丢掉 encoder/adaptation path，不是这条 policy 的 faithful deploy artifact

Control frequency:

- `sim.dt = 0.005 s`，物理 `200 Hz`
- `decimation = 4`
- policy/control 频率：`1 / (0.005 * 4) = 50 Hz`
- 控制模式：低层 `12` 关节位置 target，通过固定 PD/position actuator 执行；不是 high-level gait API
- Kp/Kd 是固定值，不是 policy 学出来的动作
- Isaac 训练/评估 actuator gains：hip/thigh `100 / 3.5`，calf `160 / 7.0`
- 当前导出 `policy.json` 记录的 MuJoCo gains：hip/thigh `60 / 2.45`，calf `96 / 4.9`

Safety/start pose:

- 默认站立姿态就是上面的 `default_joint_pos`
- base reset pose：位置 `[0,0,0.5]`，姿态 `[1,0,0,0]`
- 启动前机器人应尽量贴近这套中立站姿，机身基本水平、四足落地
- 当前 repo 没有内建急停/安全下蹲逻辑
- policy 切换时不需要 reset hidden state，但必须 reset observation history、delay buffer、`last_action`
- 当前 MuJoCo runtime 会 warm start：把初始观测重复填满 `32` 帧 history，并把 `last_action` 清零
