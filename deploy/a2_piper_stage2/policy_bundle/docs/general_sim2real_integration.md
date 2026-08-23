# GeneralSim2Real 集成边界

交付时，公开远端中用户描述的 `codex/a2-piper-stage2-dual-policy-deploy-20260823/deploy/a2_piper_stage2`、`policy_manifest.template.yaml` 和 `export_support.py` 均不可访问。公开基础分支只有原有单策略 A2 contract（`46×32 -> 12`），它会拒绝本 Stage2 dimensions。

因此本包提供的是 LMP-authoritative manifest，而不是声称已经通过不可见的 downstream YAML parser。部署侧 AI 应把本包复制到其 `policy_bundle/`，再将 `policy_manifest.yaml` 机械映射到实际模板；遇到字段名差异时以本 manifest 的 slice、顺序、公式和 parity 为准，不得退回旧 A2 policy contract。

基础分支中可复用的真实边界：

- A2 low-level joint loop 继续留在 A2；
- PC2 PiPER bridge `/piper/joint_states` / `/piper/joint_command` 提供 j1–j6 absolute-radian 50 Hz interface；
- PC2 bridge v1 不含 gripper；
- 外部策略机必须新增 synchronized A2+PiPER snapshot 和 dual-policy arm-first runtime。

现有单策略 A2 node、旧 `policy/A2_policy/policy.pt` 和其 `policy.json` 均不是 Stage2 fallback。
