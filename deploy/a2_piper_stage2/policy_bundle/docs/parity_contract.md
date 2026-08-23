# Reference parity contract

两个 NPZ 都使用 `float32` actor data，history 为 frame-major oldest-to-newest。

## `parity/arm_reference.npz`

主要 key：

- `actor_input`: `[1,600]`
- `observation_frames`: `[30,20]`
- `actor_output_raw`: arm actor final linear-layer output，plan 尚未 tanh
- `actor_output`: `[1,8]`，plan 已在 TorchScript 内 tanh
- `final_joint_target_rad`: `[1,6]`
- `body_pitch_roll_command_rad`: `[1,2]`

## `parity/dog_reference.npz`

主要 key：

- `committed_observation_frames`: policy tick 开始时的 `[30,54]` persistent history
- `preview_current_observation`: 与 committed 最新帧相同物理状态、但注入新 arm plan 的 `[1,54]` frame
- `actor_observation_frames`: `committed[1:] + preview` 的 `[30,54]`
- `actor_input`: `[1,1620]`
- `actor_output`: `[1,12]`
- `final_joint_target_rad`: `[1,12]`
- `root_quaternion_xyzw`、`rotation_body_to_world`、`projected_gravity_body`: quaternion/frame parity 数据

全部 key、shape 和 dtype 的机器可读清单位于 `metadata/lmp_source_contract.json`。
