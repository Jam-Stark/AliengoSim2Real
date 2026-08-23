"""TorchScript loading, inference, parity, and latency measurement."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np

from .contract import PolicyContract


class TorchScriptActors:
    def __init__(self, contract: PolicyContract) -> None:
        import torch

        self._torch = torch
        self._contract = contract
        self._dog = torch.jit.load(
            str(contract.bundle_dir / contract.dog.file), map_location="cpu"
        ).eval()
        self._arm = torch.jit.load(
            str(contract.bundle_dir / contract.arm.file), map_location="cpu"
        ).eval()
        for name, actor in (("dog", self._dog), ("arm", self._arm)):
            for parameter in actor.parameters():
                if not torch.isfinite(parameter).all():
                    raise ValueError(f"{name} actor contains a non-finite parameter")
        self._probe_shapes()

    def infer_arm(self, actor_input: np.ndarray) -> np.ndarray:
        return self._infer("arm", self._arm, actor_input, self._contract.arm.input_dim)

    def infer_dog(self, actor_input: np.ndarray) -> np.ndarray:
        return self._infer("dog", self._dog, actor_input, self._contract.dog.input_dim)

    def _infer(self, name: str, actor: Any, actor_input: np.ndarray, input_dim: int) -> np.ndarray:
        array = np.asarray(actor_input, dtype=np.float32)
        if array.shape != (1, input_dim):
            raise ValueError(f"{name} actor input must have shape (1, {input_dim}), got {array.shape}")
        if not np.isfinite(array).all():
            raise ValueError(f"{name} actor input contains a non-finite value")
        with self._torch.inference_mode():
            output = actor(self._torch.from_numpy(array)).detach().cpu().numpy()
        expected = (1, self._contract.dog.output_dim if name == "dog" else self._contract.arm.output_dim)
        if output.shape != expected:
            raise ValueError(f"{name} actor output must have shape {expected}, got {output.shape}")
        if not np.isfinite(output).all():
            raise ValueError(f"{name} actor output contains a non-finite value")
        return output[0].astype(np.float32, copy=False)

    def _probe_shapes(self) -> None:
        self.infer_dog(np.zeros((1, self._contract.dog.input_dim), dtype=np.float32))
        self.infer_arm(np.zeros((1, self._contract.arm.input_dim), dtype=np.float32))

    def benchmark(self, warmup_pairs: int, measured_pairs: int) -> dict[str, Any]:
        arm_input = np.zeros((1, self._contract.arm.input_dim), dtype=np.float32)
        dog_input = np.zeros((1, self._contract.dog.input_dim), dtype=np.float32)
        for _ in range(warmup_pairs):
            self.infer_arm(arm_input)
            self.infer_dog(dog_input)
        samples_ms = np.empty(measured_pairs, dtype=np.float64)
        for index in range(measured_pairs):
            started = time.perf_counter_ns()
            self.infer_arm(arm_input)
            self.infer_dog(dog_input)
            samples_ms[index] = (time.perf_counter_ns() - started) / 1_000_000.0
        return {
            "status": "pass",
            "device": "cpu",
            "torch": self._torch.__version__,
            "warmup_pairs": warmup_pairs,
            "measured_pairs": measured_pairs,
            "latency_ms": {
                "mean": float(np.mean(samples_ms)),
                "p50": float(np.percentile(samples_ms, 50)),
                "p95": float(np.percentile(samples_ms, 95)),
                "p99": float(np.percentile(samples_ms, 99)),
                "max": float(np.max(samples_ms)),
            },
            "policy_period_ms": self._contract.policy_period_s * 1000.0,
            "host_specific": True,
        }


def run_bundle_validator(bundle_dir: str | Path, tolerance: float = 1.0e-6) -> dict[str, Any]:
    root = Path(bundle_dir).expanduser().resolve()
    script = root / "tools" / "validate_bundle.py"
    completed = subprocess.run(
        [sys.executable, str(script), str(root), "--atol", str(tolerance)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def validate_contract_references(
    contract: PolicyContract, tolerance: float = 1.0e-6
) -> dict[str, Any]:
    """Check manifest-driven semantics that the shipped validator hard-codes or omits."""

    with np.load(contract.bundle_dir / "parity" / "dog_reference.npz", allow_pickle=False) as data:
        dog = {key: data[key] for key in data.files}
    with np.load(contract.bundle_dir / "parity" / "arm_reference.npz", allow_pickle=False) as data:
        arm = {key: data[key] for key in data.files}

    errors: dict[str, float] = {}

    def compare(name: str, actual: np.ndarray, expected: np.ndarray) -> None:
        if actual.shape != expected.shape:
            raise ValueError(f"{name} shape mismatch: {actual.shape} != {expected.shape}")
        error = float(np.max(np.abs(actual - expected)))
        if error > tolerance:
            raise ValueError(f"{name} max absolute error {error} exceeds {tolerance}")
        errors[name] = error

    dog_nominal_target = np.clip(
        contract.dog.default_position_rad + contract.dog.action_scale_rad * dog["actor_output"],
        *contract.dog.processed_clip_rad,
    ).astype(np.float32)
    arm_nominal_target = np.clip(
        contract.arm.default_position_rad
        + contract.arm.action_scale_rad * arm["actor_output"][:, 0:6],
        *contract.arm.processed_clip_rad,
    ).astype(np.float32)
    compare("manifest_dog_nominal_target", dog_nominal_target, dog["final_joint_target_rad"])
    compare("manifest_arm_nominal_target", arm_nominal_target, arm["final_joint_target_rad"])

    plan = (np.clip(arm["actor_output"][:, 6:8], -1.0, 1.0) * 0.4).astype(np.float32)
    compare("manifest_arm_plan", plan, arm["body_pitch_roll_command_rad"])
    compare("manifest_arm_plan_to_dog_preview", plan, dog["preview_command_raw"][:, 3:5])

    preview_frames = np.concatenate(
        (dog["committed_observation_frames"][1:], dog["preview_current_observation"]), axis=0
    )
    compare("manifest_dog_29_plus_1_frames", preview_frames, dog["actor_observation_frames"])
    compare(
        "manifest_dog_frame_major_flatten",
        preview_frames.reshape(1, contract.dog.input_dim),
        dog["actor_input"],
    )
    compare(
        "manifest_arm_frame_major_flatten",
        arm["observation_frames"].reshape(1, contract.arm.input_dim),
        arm["actor_input"],
    )

    quaternion = dog["root_quaternion_xyzw"].astype(np.float32)
    quaternion = quaternion / np.linalg.norm(quaternion, axis=1, keepdims=True)
    x, y, z, w = (quaternion[:, index] for index in range(4))
    gravity = np.stack(
        (
            2.0 * (w * y - x * z),
            -2.0 * (y * z + w * x),
            2.0 * (x * x + y * y) - 1.0,
        ),
        axis=1,
    ).astype(np.float32)
    compare("manifest_projected_gravity_xyzw", gravity, dog["projected_gravity_body"])

    return {
        "status": "pass",
        "tolerance": tolerance,
        "max_absolute_errors": errors,
        "nominal_target_scope": "default_plus_scaled_raw_before_site_hard_and_rate_limits",
    }
