#!/usr/bin/env python3
"""Validate the exported A2 + Piper Stage2 deployment bundle on CPU."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import torch


_DOG_DEFAULT = np.asarray(
    [0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, 0.5, -1.0, -1.0, -1.0, -1.0],
    dtype=np.float32,
)
_ARM_DEFAULT = np.asarray([0.0, 1.48, -0.63, -0.84, 0.0, 1.57], dtype=np.float32)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle_dir", type=Path)
    parser.add_argument("--atol", type=float, default=1.0e-6)
    return parser.parse_args()


def _assert_close(name: str, actual: np.ndarray, expected: np.ndarray, atol: float) -> float:
    if actual.shape != expected.shape:
        raise ValueError(f"{name} shape mismatch: actual={actual.shape}, expected={expected.shape}.")
    maximum_error = float(np.max(np.abs(actual - expected)))
    if maximum_error > atol:
        raise ValueError(f"{name} max absolute error {maximum_error} exceeds tolerance {atol}.")
    return maximum_error


def _load_reference(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=False) as archive:
        return {key: archive[key] for key in archive.files}


def main() -> None:
    args = _parse_args()
    bundle_dir = args.bundle_dir.expanduser().resolve()
    dog_reference = _load_reference(bundle_dir / "parity/dog_reference.npz")
    arm_reference = _load_reference(bundle_dir / "parity/arm_reference.npz")
    dog_actor = torch.jit.load(str(bundle_dir / "dog_actor.pt"), map_location="cpu").eval()
    arm_actor = torch.jit.load(str(bundle_dir / "arm_actor.pt"), map_location="cpu").eval()

    for actor_name, actor in (("dog", dog_actor), ("arm", arm_actor)):
        for parameter in actor.parameters():
            if not torch.isfinite(parameter).all():
                raise ValueError(f"{actor_name}_actor.pt contains a non-finite parameter.")

    with torch.inference_mode():
        dog_output = dog_actor(torch.from_numpy(dog_reference["actor_input"])).numpy()
        arm_output = arm_actor(torch.from_numpy(arm_reference["actor_input"])).numpy()

    errors = {
        "dog_actor_output": _assert_close(
            "dog actor output", dog_output, dog_reference["actor_output"], args.atol
        ),
        "arm_actor_output": _assert_close(
            "arm actor output", arm_output, arm_reference["actor_output"], args.atol
        ),
    }

    dog_target = np.clip(_DOG_DEFAULT + 0.25 * dog_output, -100.0, 100.0).astype(np.float32)
    arm_target = np.clip(_ARM_DEFAULT + 0.25 * arm_output[:, :6], -100.0, 100.0).astype(np.float32)
    plan_command = (0.4 * np.clip(arm_output[:, 6:8], -1.0, 1.0)).astype(np.float32)
    errors["dog_final_joint_target"] = _assert_close(
        "dog final joint target", dog_target, dog_reference["final_joint_target_rad"], args.atol
    )
    errors["arm_final_joint_target"] = _assert_close(
        "arm final joint target", arm_target, arm_reference["final_joint_target_rad"], args.atol
    )
    errors["arm_plan_command"] = _assert_close(
        "arm plan command", plan_command, arm_reference["body_pitch_roll_command_rad"], args.atol
    )
    errors["arm_plan_to_dog_preview"] = _assert_close(
        "arm plan to dog preview",
        plan_command,
        dog_reference["preview_command_raw"][:, 3:5],
        args.atol,
    )

    expected_actor_frames = np.concatenate(
        (
            dog_reference["committed_observation_frames"][1:],
            dog_reference["preview_current_observation"],
        ),
        axis=0,
    )
    errors["dog_preview_history"] = _assert_close(
        "dog preview history",
        dog_reference["actor_observation_frames"],
        expected_actor_frames,
        args.atol,
    )
    errors["dog_flattened_history"] = _assert_close(
        "dog flattened history",
        dog_reference["actor_input"],
        expected_actor_frames.reshape(1, 1620),
        args.atol,
    )

    gravity_from_rotation = (
        dog_reference["rotation_body_to_world"].transpose(0, 2, 1)
        @ dog_reference["gravity_world_unit"]
    ).astype(np.float32)
    errors["projected_gravity"] = _assert_close(
        "projected gravity",
        gravity_from_rotation,
        dog_reference["projected_gravity_body"],
        args.atol,
    )

    report = {
        "status": "pass",
        "device": "cpu",
        "torch": torch.__version__,
        "dog_input_shape": list(dog_reference["actor_input"].shape),
        "dog_output_shape": list(dog_output.shape),
        "arm_input_shape": list(arm_reference["actor_input"].shape),
        "arm_output_shape": list(arm_output.shape),
        "tolerance": args.atol,
        "max_absolute_errors": errors,
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
