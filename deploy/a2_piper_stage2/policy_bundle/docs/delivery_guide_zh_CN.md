# 部署侧 AI 接收指南

## 1. 解压与只读检查

保持目录结构不变。确认根目录至少包含：

```text
dog_actor.pt
arm_actor.pt
policy_manifest.yaml
parity/dog_reference.npz
parity/arm_reference.npz
metadata/export_versions.yaml
metadata/lmp_source_contract.json
tools/validate_bundle.py
```

不要把训练 runner checkpoint 当成模型；本包已把在线推理所需 actor-side modules 单独导出。

## 2. 建立 CPU parity 环境

导出环境是 Python `3.11.15`、PyTorch `2.7.0+cu128`、NumPy `1.26.0`。部署首轮可使用官方 CPU wheel，但必须以 parity 结果作为兼容性判据：

```bash
python -m pip install --index-url https://download.pytorch.org/whl/cpu torch==2.7.0
python -m pip install numpy==1.26.0 PyYAML==6.0.2
python tools/validate_bundle.py .
```

不要因为模型能 `torch.jit.load()` 就跳过 parity。validator 同时核对：

- dog/arm input、output shape；
- raw actor action；
- position offset/scale 后的 joint target；
- arm plan postprocess；
- arm plan 到 dog same-tick preview；
- dog 29 committed frames + 1 preview frame；
- ROS `xyzw` quaternion 对应的 projected gravity。

## 3. 填入部署 manifest

`policy_manifest.yaml` 是 LMP 侧 authoritative source contract。部署侧可以机械映射字段名到自己的 schema，但不得修改数值语义。特别检查：

1. dog frame `54`、history `30`、flatten `1620`；
2. arm frame `20`、history `30`、flatten `600`；
3. frame-major oldest-to-newest；
4. no observation normalization/noise/clip；
5. arm-first、dog-second；
6. dog preview 采用 drop-oldest + append-preview；
7. raw action cache 与 hardware target cache 分离；
8. action joint order按 manifest，不按 URDF 自然顺序；
9. arm output `[6:8]` 是 `[body_pitch, body_roll]`，不是 joint/gripper；
10. gripper没有 policy output。

## 4. 状态装配

同一 logical tick 必须使用一个同步快照：A2 root-link attitude、12 个腿关节 q/qd、PiPER j1–j6 q。演员输入中没有 base velocity、TCP pose、arm qd 或 contact；不要为了“填满字段”添加这些量。

ROS quaternion 是 `xyzw`。先用 `parity/dog_reference.npz` 中的 `root_quaternion_xyzw` 和 `projected_gravity_body` 验证 IMU/root-link frame 变换，再接真实状态。传感器若给的是 IMU frame，必须先应用已验证的 IMU→root-link 固定变换。

## 5. Action 与 bridge 边界

- Dog：`q_target = clip(q_default + 0.25*dog_raw, -100, 100)`，再应用命名 joint hard/rate limits。
- Arm：只使用 output `[0:6]` 形成 `arm_j1..j6` target；PC2 bridge 接收 absolute radians。
- Plan：只写内部 dog command pitch/roll，不发布到 PiPER joint topic。
- Gripper：PC2 bridge v1 没有 gripper interface；不得把 plan 两维映射成 gripper。
- Bridge 不得重复应用 `0.25` 或 default offset。

manifest 内 hard ranges 来自 active LMP URDF，training velocity/effort limits 来自 Isaac actuator config；它们不是实机硬件认证。部署侧必须与现场 bridge/厂商限制取交集。

## 6. Gate 顺序

完成 bundle parity 后仍应依次执行部署仓库的 mock、benchmark、只读 state、10 分钟 shadow、单组件 live 和支撑状态 coupled gate。本包不授权设置 `output_enabled: true`，也不授权使用 `--live`。
