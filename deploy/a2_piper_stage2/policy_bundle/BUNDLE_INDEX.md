# Bundle index

Release: `a2_piper_stage2_policy_bundle_020000_20260823`

Created: `2026-08-23 23:30 HKT`

Payload before bundle control files: `9,618,052 bytes`。本包远低于 95 MiB 单文件交付上限，因此只生成一个普通 ZIP。

## Models

- `dog_actor.pt` (`5,856,461 bytes`): TorchScript dog deterministic actor，`[B,1620] -> [B,12]`。
- `arm_actor.pt` (`3,605,821 bytes`): TorchScript arm deterministic actor，`[B,600] -> [B,8]`。

## Contract and parity

- `policy_manifest.yaml`: LMP-authoritative Stage2 runtime contract。
- `parity/dog_reference.npz`: dog committed/preview history、quaternion/gravity、raw action、target reference。
- `parity/arm_reference.npz`: arm history、raw/control/plan、target reference。
- `metadata/lmp_source_contract.json`: full source-derived contract and NPZ key schemas。
- `metadata/export_versions.yaml`: checkpoint/export/runtime versions。
- `metadata/export_validation.json`: offline parity and CPU benchmark receipt。

## Integration support

- `tools/validate_bundle.py`: standalone CPU bundle parity validator。
- `tools/export_stage2_deployment_bundle.py`: exact LMP export implementation used for this handoff。
- `docs/delivery_guide_zh_CN.md`: deployment-side AI execution guide。
- `docs/parity_contract.md`: reference key semantics。
- `docs/general_sim2real_integration.md`: known downstream integration boundary。
- `README.md`, `PRO_HANDOFF.md`: entrypoint and evidence summary。

Training checkpoint、critic、optimizer、credentials、site config 和 live enablement 均未打包。
