#!/usr/bin/env python3
"""Export the Stage2 dual-policy deployment bundle from runner_state_020000.pt."""

from __future__ import annotations

import argparse
import json
import platform
import re
from collections.abc import Mapping
from pathlib import Path
from typing import Any

import numpy as np
import torch
import torch.nn as nn


_CHECKPOINT_ITERATION = 20_000
_CHECKPOINT_FORMAT_VERSION = 2
_BUNDLE_FORMAT_VERSION = 1
_REFERENCE_FORMAT_VERSION = 1

_DOG_FRAME_DIM = 54
_DOG_HISTORY_LENGTH = 30
_DOG_HISTORY_DIM = _DOG_FRAME_DIM * _DOG_HISTORY_LENGTH
_DOG_ACTION_DIM = 12
_DOG_PRIVILEGED_DIM = 25

_ARM_FRAME_DIM = 20
_ARM_HISTORY_LENGTH = 30
_ARM_HISTORY_DIM = _ARM_FRAME_DIM * _ARM_HISTORY_LENGTH
_ARM_CONTROL_DIM = 6
_ARM_PLAN_DIM = 2
_ARM_ACTION_DIM = _ARM_CONTROL_DIM + _ARM_PLAN_DIM
_ARM_PRIVILEGED_DIM = 9

_DOG_JOINTS = (
    "FL_hip_joint",
    "FR_hip_joint",
    "RL_hip_joint",
    "RR_hip_joint",
    "FL_thigh_joint",
    "FR_thigh_joint",
    "RL_thigh_joint",
    "RR_thigh_joint",
    "FL_calf_joint",
    "FR_calf_joint",
    "RL_calf_joint",
    "RR_calf_joint",
)
_DOG_DEFAULT_JOINT_POS = (0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, 0.5, -1.0, -1.0, -1.0, -1.0)
_ARM_JOINTS = ("arm_j1", "arm_j2", "arm_j3", "arm_j4", "arm_j5", "arm_j6")
_ARM_DEFAULT_JOINT_POS = (0.0, 1.48, -0.63, -0.84, 0.0, 1.57)


def _build_mlp(dimensions: tuple[int, ...]) -> nn.Sequential:
    layers: list[nn.Module] = []
    for layer_index in range(len(dimensions) - 1):
        layers.append(nn.Linear(dimensions[layer_index], dimensions[layer_index + 1]))
        if layer_index + 2 < len(dimensions):
            layers.append(nn.ELU())
    return nn.Sequential(*layers)


class DogDeterministicActor(nn.Module):
    """Batch dog actor mean: ``(B, 1620) -> (B, 12)``."""

    def __init__(self) -> None:
        super().__init__()
        self.adaptation_module = _build_mlp((_DOG_HISTORY_DIM, 256, 128, _DOG_PRIVILEGED_DIM))
        self.actor_body = _build_mlp(
            (_DOG_HISTORY_DIM + _DOG_PRIVILEGED_DIM, 512, 256, 128, _DOG_ACTION_DIM)
        )

    def forward(self, observation_history: torch.Tensor) -> torch.Tensor:
        if observation_history.dim() != 2 or observation_history.size(1) != 1620:
            raise RuntimeError("Dog actor expects observation_history shape (B, 1620).")
        latent = self.adaptation_module(observation_history)
        return self.actor_body(torch.cat((observation_history, latent), dim=-1))


class ArmDeterministicActor(nn.Module):
    """Batch arm actor mean: ``(B, 600) -> (B, 8)`` with plan tanh."""

    def __init__(self) -> None:
        super().__init__()
        self.adaptation_module = _build_mlp((_ARM_HISTORY_DIM, 256, 128, _ARM_PRIVILEGED_DIM))
        self.actor_history_encoder = _build_mlp(
            (_ARM_HISTORY_DIM - _ARM_FRAME_DIM, 512, 256, 128)
        )
        self.actor_body = _build_mlp(
            (_ARM_FRAME_DIM + _ARM_PRIVILEGED_DIM + 128, 512, 256, 128, _ARM_ACTION_DIM)
        )

    def forward_raw(self, observation_history: torch.Tensor) -> torch.Tensor:
        current_observation = observation_history[:, -20:]
        history_only = observation_history[:, :-20]
        latent = self.adaptation_module(observation_history)
        history_latent = self.actor_history_encoder(history_only)
        return self.actor_body(torch.cat((current_observation, latent, history_latent), dim=-1))

    def forward(self, observation_history: torch.Tensor) -> torch.Tensor:
        if observation_history.dim() != 2 or observation_history.size(1) != 600:
            raise RuntimeError("Arm actor expects observation_history shape (B, 600).")
        raw_action = self.forward_raw(observation_history)
        return torch.cat((raw_action[:, :6], torch.tanh(raw_action[:, 6:])), dim=-1)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export Stage2 TorchScript actors, deterministic parity references, and the LMP source contract."
    )
    parser.add_argument(
        "--log-dir",
        type=Path,
        required=True,
        help="Training run directory containing checkpoints_meta/runner_state_020000.pt.",
    )
    parser.add_argument(
        "--checkpoint",
        default="020000",
        help="Authoritative numbered checkpoint. This exporter requires iteration 020000.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Bundle output directory.",
    )
    return parser.parse_args()


def _resolve_checkpoint(log_dir: Path, checkpoint: str) -> tuple[Path, Path]:
    checkpoint = str(checkpoint)
    if re.fullmatch(r"[0-9]{1,6}", checkpoint) is None:
        raise ValueError(f"--checkpoint must be numeric, received {checkpoint!r}.")
    if int(checkpoint) != _CHECKPOINT_ITERATION:
        raise ValueError(f"This Stage2 deployment exporter requires checkpoint 020000, received {checkpoint!r}.")

    log_dir = log_dir.expanduser().resolve()
    checkpoint_path = log_dir / "checkpoints_meta" / "runner_state_020000.pt"
    if not checkpoint_path.is_file():
        raise FileNotFoundError(f"Authoritative runner checkpoint does not exist: {checkpoint_path}")
    return log_dir, checkpoint_path.resolve()


def _load_runner_state(checkpoint_path: Path) -> Mapping[str, Any]:
    state = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    if not isinstance(state, Mapping):
        raise TypeError(f"Runner checkpoint must contain a mapping, received {type(state).__name__}.")

    required_keys = (
        "checkpoint_format_version",
        "dog_model",
        "arm_model",
        "iteration",
        "tot_timesteps",
        "switch_scheduler",
        "runner_cfg",
    )
    missing = [key for key in required_keys if key not in state]
    if missing:
        raise KeyError(f"Runner checkpoint is missing required keys: {missing}")
    if state["checkpoint_format_version"] != _CHECKPOINT_FORMAT_VERSION:
        raise ValueError(
            f"Expected checkpoint format {_CHECKPOINT_FORMAT_VERSION}, received {state['checkpoint_format_version']}."
        )
    if state["iteration"] != _CHECKPOINT_ITERATION:
        raise ValueError(f"Expected embedded iteration 20000, received {state['iteration']}.")
    if not isinstance(state["tot_timesteps"], int):
        raise TypeError("Runner checkpoint tot_timesteps must be an integer.")
    if not isinstance(state["switch_scheduler"], Mapping):
        raise TypeError("Runner checkpoint switch_scheduler must be a mapping.")
    if not isinstance(state["runner_cfg"], Mapping):
        raise TypeError("Runner checkpoint runner_cfg must be a mapping.")
    return state


def _load_actor_parameters(
    actor: nn.Module,
    checkpoint_model_state: Any,
    model_key: str,
) -> None:
    if not isinstance(checkpoint_model_state, Mapping):
        raise TypeError(f"Checkpoint field {model_key!r} must be a model state mapping.")

    expected_state = actor.state_dict()
    expected_keys = set(expected_state)
    selected_state = {key: value for key, value in checkpoint_model_state.items() if key in expected_keys}
    missing = sorted(expected_keys - set(selected_state))
    if missing:
        raise KeyError(f"Checkpoint field {model_key!r} is missing deterministic actor parameters: {missing}")

    actor_module_prefixes = tuple(f"{name}." for name, _ in actor.named_children())
    unexpected = sorted(
        key
        for key in checkpoint_model_state
        if key.startswith(actor_module_prefixes) and key not in expected_keys
    )
    if unexpected:
        raise ValueError(f"Checkpoint field {model_key!r} has unexpected actor parameters: {unexpected}")

    for key, expected_tensor in expected_state.items():
        checkpoint_tensor = selected_state[key]
        if not isinstance(checkpoint_tensor, torch.Tensor):
            raise TypeError(f"Checkpoint parameter {model_key}.{key} is not a torch.Tensor.")
        if checkpoint_tensor.dtype != torch.float32:
            raise TypeError(f"Checkpoint parameter {model_key}.{key} must be float32, got {checkpoint_tensor.dtype}.")
        if checkpoint_tensor.shape != expected_tensor.shape:
            raise ValueError(
                f"Checkpoint parameter {model_key}.{key} has shape {tuple(checkpoint_tensor.shape)}, "
                f"expected {tuple(expected_tensor.shape)}."
            )

    actor.load_state_dict(selected_state, strict=True)
    actor.eval()


def _euler_xyz_to_quaternion_xyzw(
    roll: np.ndarray,
    pitch: np.ndarray,
    yaw: np.ndarray,
) -> np.ndarray:
    half_roll = 0.5 * roll
    half_pitch = 0.5 * pitch
    half_yaw = 0.5 * yaw
    cr, sr = np.cos(half_roll), np.sin(half_roll)
    cp, sp = np.cos(half_pitch), np.sin(half_pitch)
    cy, sy = np.cos(half_yaw), np.sin(half_yaw)
    quaternion = np.stack(
        (
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy,
        ),
        axis=-1,
    ).astype(np.float32)
    quaternion /= np.linalg.norm(quaternion, axis=-1, keepdims=True)
    return quaternion


def _rotation_body_to_world(quaternion_xyzw: np.ndarray) -> np.ndarray:
    x, y, z, w = (quaternion_xyzw[:, index] for index in range(4))
    rotation = np.empty((quaternion_xyzw.shape[0], 3, 3), dtype=np.float32)
    rotation[:, 0, 0] = 1.0 - 2.0 * (y * y + z * z)
    rotation[:, 0, 1] = 2.0 * (x * y - z * w)
    rotation[:, 0, 2] = 2.0 * (x * z + y * w)
    rotation[:, 1, 0] = 2.0 * (x * y + z * w)
    rotation[:, 1, 1] = 1.0 - 2.0 * (x * x + z * z)
    rotation[:, 1, 2] = 2.0 * (y * z - x * w)
    rotation[:, 2, 0] = 2.0 * (x * z - y * w)
    rotation[:, 2, 1] = 2.0 * (y * z + x * w)
    rotation[:, 2, 2] = 1.0 - 2.0 * (x * x + y * y)
    return rotation


def _trajectory_primitives() -> dict[str, np.ndarray]:
    frame_index = np.arange(_DOG_HISTORY_LENGTH, dtype=np.int32)
    phase_index = frame_index.astype(np.float32)
    frame_time_s = phase_index * np.float32(0.02)
    roll = 0.12 * np.sin(0.17 * phase_index) + 0.02 * np.cos(0.07 * phase_index)
    pitch = -0.10 * np.cos(0.13 * phase_index) + 0.03 * np.sin(0.05 * phase_index)
    yaw = 0.25 * np.sin(0.09 * phase_index) + 0.01 * phase_index
    base_roll_pitch = np.stack((roll, pitch), axis=-1).astype(np.float32)
    quaternion_xyzw = _euler_xyz_to_quaternion_xyzw(roll, pitch, yaw)
    rotation = _rotation_body_to_world(quaternion_xyzw)
    gravity_world_unit = np.asarray((0.0, 0.0, -1.0), dtype=np.float32)
    projected_gravity_body = (rotation.transpose(0, 2, 1) @ gravity_world_unit).astype(np.float32)
    return {
        "frame_index": frame_index,
        "frame_time_s": frame_time_s,
        "base_euler_xyz_rad": np.stack((roll, pitch, yaw), axis=-1).astype(np.float32),
        "base_roll_pitch_rad": base_roll_pitch,
        "root_quaternion_xyzw": quaternion_xyzw,
        "gravity_world_unit": gravity_world_unit,
        "rotation_body_to_world": rotation,
        "projected_gravity_body": projected_gravity_body,
    }


def _build_dog_reference(
    actor: DogDeterministicActor,
    primitives: Mapping[str, np.ndarray],
    arm_reference: Mapping[str, np.ndarray],
) -> dict[str, np.ndarray]:
    frame_index = primitives["frame_index"].astype(np.float32)[:, None]
    joint_index = np.arange(_DOG_ACTION_DIM, dtype=np.float32)[None, :]
    default_joint_pos = np.asarray(_DOG_DEFAULT_JOINT_POS, dtype=np.float32)
    joint_position_relative = (
        0.12 * np.sin(0.19 * frame_index + 0.27 * joint_index)
        + 0.03 * np.cos(0.07 * frame_index - 0.11 * joint_index)
    ).astype(np.float32)
    joint_position = joint_position_relative + default_joint_pos
    joint_velocity = (
        0.8 * np.cos(0.19 * frame_index + 0.27 * joint_index)
        - 0.12 * np.sin(0.07 * frame_index - 0.11 * joint_index)
    ).astype(np.float32)
    joint_velocity_observation = (joint_velocity * np.float32(0.05)).astype(np.float32)
    last_action = (0.35 * np.sin(0.23 * frame_index + 0.31 * joint_index)).astype(np.float32)

    time_index = frame_index[:, 0]
    command_raw = np.stack(
        (
            0.45 + 0.18 * np.sin(0.11 * time_index),
            0.20 * np.cos(0.09 * time_index),
            0.16 * np.sin(0.15 * time_index),
            0.12 * np.sin(0.13 * time_index),
            -0.10 * np.cos(0.17 * time_index),
        ),
        axis=-1,
    ).astype(np.float32)
    command_scale = np.asarray((2.0, 2.0, 0.25, 1.0, 1.0), dtype=np.float32)
    command_observation = command_raw * command_scale
    arm_goal = np.stack(
        (
            0.62 + 0.06 * np.sin(0.08 * time_index),
            0.22 * np.sin(0.10 * time_index),
            -0.28 * np.cos(0.07 * time_index),
            np.zeros_like(time_index),
            np.zeros_like(time_index),
            np.zeros_like(time_index),
        ),
        axis=-1,
    ).astype(np.float32)
    gait_phase = np.remainder(time_index * np.float32(0.02 * 2.0), np.float32(1.0)).astype(np.float32)
    gait_clock = np.stack(
        (np.sin(2.0 * np.pi * gait_phase), np.cos(2.0 * np.pi * gait_phase)), axis=-1
    ).astype(np.float32)

    committed_frames = np.concatenate(
        (
            primitives["projected_gravity_body"],
            joint_position_relative,
            joint_velocity_observation,
            last_action,
            command_observation,
            arm_goal,
            primitives["base_roll_pitch_rad"],
            gait_clock,
        ),
        axis=-1,
    ).astype(np.float32)
    if committed_frames.shape != (_DOG_HISTORY_LENGTH, _DOG_FRAME_DIM):
        raise RuntimeError(f"Dog committed reference frames have unexpected shape {committed_frames.shape}.")

    arm_plan = np.asarray(arm_reference["arm_plan"], dtype=np.float32)
    body_pitch_roll_command = np.asarray(arm_reference["body_pitch_roll_command_rad"], dtype=np.float32)
    if arm_plan.shape != (1, _ARM_PLAN_DIM) or body_pitch_roll_command.shape != (1, _ARM_PLAN_DIM):
        raise RuntimeError("Arm parity reference must expose one 2-D plan and body pitch/roll command.")
    preview_command_raw = command_raw[-1:, :].copy()
    preview_command_raw[:, 3:5] = body_pitch_roll_command
    preview_command_observation = preview_command_raw * command_scale
    preview_current_observation = committed_frames[-1:, :].copy()
    preview_current_observation[:, 39:44] = preview_command_observation
    actor_frames = np.concatenate((committed_frames[1:, :], preview_current_observation), axis=0)
    actor_input = actor_frames.reshape(1, _DOG_HISTORY_DIM)
    with torch.inference_mode():
        actor_output = actor(torch.from_numpy(actor_input)).numpy()
    final_joint_target = np.clip(
        default_joint_pos[None, :] + np.float32(0.25) * actor_output,
        np.float32(-100.0),
        np.float32(100.0),
    ).astype(np.float32)

    return {
        **primitives,
        "default_joint_position_rad": default_joint_pos,
        "joint_position_rad": joint_position,
        "joint_position_relative_rad": joint_position_relative,
        "joint_velocity_rad_s": joint_velocity,
        "joint_velocity_observation": joint_velocity_observation,
        "last_action": last_action,
        "command_raw": command_raw,
        "command_observation": command_observation,
        "preview_command_raw": preview_command_raw,
        "preview_command_observation": preview_command_observation,
        "arm_goal_observation": arm_goal,
        "gait_phase": gait_phase,
        "gait_clock": gait_clock,
        "arm_plan": arm_plan,
        "body_pitch_roll_command_rad": body_pitch_roll_command,
        "committed_observation_frames": committed_frames,
        "committed_history_input": committed_frames.reshape(1, _DOG_HISTORY_DIM),
        "preview_current_observation": preview_current_observation,
        "actor_observation_frames": actor_frames,
        "actor_root_quaternion_xyzw": np.concatenate(
            (primitives["root_quaternion_xyzw"][1:, :], primitives["root_quaternion_xyzw"][-1:, :]), axis=0
        ),
        "actor_projected_gravity_body": np.concatenate(
            (primitives["projected_gravity_body"][1:, :], primitives["projected_gravity_body"][-1:, :]), axis=0
        ),
        "actor_input": actor_input,
        "current_observation": preview_current_observation,
        "actor_output_raw": actor_output.copy(),
        "actor_output": actor_output,
        "final_joint_target_rad": final_joint_target,
    }


def _build_arm_reference(actor: ArmDeterministicActor, primitives: Mapping[str, np.ndarray]) -> dict[str, np.ndarray]:
    frame_index = primitives["frame_index"].astype(np.float32)[:, None]
    joint_index = np.arange(_ARM_CONTROL_DIM, dtype=np.float32)[None, :]
    default_joint_pos = np.asarray(_ARM_DEFAULT_JOINT_POS, dtype=np.float32)
    joint_position_relative = (
        0.14 * np.sin(0.16 * frame_index + 0.37 * joint_index)
        + 0.02 * np.cos(0.05 * frame_index - 0.19 * joint_index)
    ).astype(np.float32)
    joint_position = joint_position_relative + default_joint_pos
    last_control_action = (0.28 * np.cos(0.18 * frame_index + 0.29 * joint_index)).astype(np.float32)
    time_index = frame_index[:, 0]
    arm_goal = np.stack(
        (
            0.62 + 0.06 * np.sin(0.08 * time_index),
            0.22 * np.sin(0.10 * time_index),
            -0.28 * np.cos(0.07 * time_index),
            np.zeros_like(time_index),
            np.zeros_like(time_index),
            np.zeros_like(time_index),
        ),
        axis=-1,
    ).astype(np.float32)
    frames = np.concatenate(
        (joint_position_relative, last_control_action, arm_goal, primitives["base_roll_pitch_rad"]), axis=-1
    ).astype(np.float32)
    if frames.shape != (_ARM_HISTORY_LENGTH, _ARM_FRAME_DIM):
        raise RuntimeError(f"Arm reference frames have unexpected shape {frames.shape}.")
    actor_input = frames.reshape(1, _ARM_HISTORY_DIM)
    with torch.inference_mode():
        actor_input_torch = torch.from_numpy(actor_input)
        actor_output_raw = actor.forward_raw(actor_input_torch).numpy()
        actor_output = actor(actor_input_torch).numpy()
    arm_plan = actor_output[:, _ARM_CONTROL_DIM:]
    body_pitch_roll_command = (
        np.clip(arm_plan, np.float32(-1.0), np.float32(1.0)) * np.float32(0.4)
    ).astype(np.float32)
    final_joint_target = np.clip(
        default_joint_pos[None, :] + np.float32(0.25) * actor_output[:, :_ARM_CONTROL_DIM],
        np.float32(-100.0),
        np.float32(100.0),
    ).astype(np.float32)

    return {
        "frame_index": primitives["frame_index"],
        "frame_time_s": primitives["frame_time_s"],
        "base_euler_xyz_rad": primitives["base_euler_xyz_rad"],
        "base_roll_pitch_rad": primitives["base_roll_pitch_rad"],
        "root_quaternion_xyzw": primitives["root_quaternion_xyzw"],
        "default_joint_position_rad": default_joint_pos,
        "joint_position_rad": joint_position,
        "joint_position_relative_rad": joint_position_relative,
        "last_control_action": last_control_action,
        "arm_goal_observation": arm_goal,
        "observation_frames": frames,
        "actor_input": actor_input,
        "history_only_input": actor_input[:, :-_ARM_FRAME_DIM],
        "current_observation": actor_input[:, -_ARM_FRAME_DIM:],
        "actor_output_raw": actor_output_raw,
        "actor_output": actor_output,
        "final_joint_target_rad": final_joint_target,
        "arm_plan_pre_tanh": actor_output_raw[:, _ARM_CONTROL_DIM:],
        "arm_plan": arm_plan,
        "body_pitch_roll_command_rad": body_pitch_roll_command,
        "gripper_fixed_joint_target": np.asarray((0.0, 0.0), dtype=np.float32),
    }


def _array_schema(arrays: Mapping[str, np.ndarray]) -> list[dict[str, Any]]:
    return [
        {
            "key": key,
            "shape": list(value.shape),
            "dtype": str(value.dtype),
        }
        for key, value in arrays.items()
    ]


def _observation_field(
    name: str,
    start: int,
    end: int,
    units: str,
    semantics: str,
    **extra: Any,
) -> dict[str, Any]:
    field: dict[str, Any] = {
        "name": name,
        "slice": [start, end],
        "units": units,
        "semantics": semantics,
    }
    field.update(extra)
    return field


def _source_contract(
    checkpoint_path: Path,
    log_dir: Path,
    state: Mapping[str, Any],
    dog_reference: Mapping[str, np.ndarray],
    arm_reference: Mapping[str, np.ndarray],
) -> dict[str, Any]:
    return {
        "contract": "lmp_stage2_dual_policy_source",
        "contract_version": 1,
        "bundle_format_version": _BUNDLE_FORMAT_VERSION,
        "source": {
            "path": str(checkpoint_path),
            "log_dir": str(log_dir),
            "authoritative_pattern": "checkpoints_meta/runner_state_<6digit>.pt",
            "checkpoint": "020000",
            "checkpoint_format_version": state["checkpoint_format_version"],
            "iteration": state["iteration"],
            "tot_timesteps": state["tot_timesteps"],
            "scheduler": state["switch_scheduler"],
            "runner_cfg": state["runner_cfg"],
            "lineage": {
                "status": "unavailable",
                "reason": "The runner checkpoint does not embed a repository revision or resolved environment configuration.",
            },
        },
        "artifacts": {
            "dog_actor": "dog_actor.pt",
            "arm_actor": "arm_actor.pt",
            "dog_reference": "parity/dog_reference.npz",
            "arm_reference": "parity/arm_reference.npz",
            "export_versions": "metadata/export_versions.yaml",
            "source_contract": "metadata/lmp_source_contract.json",
            "policy_manifest": {
                "status": "not_emitted",
                "reason": "The downstream deployment schema has not been provided.",
            },
        },
        "torchscript": {
            "dtype": "float32",
            "device": "cpu",
            "method": "forward",
            "batch_axis": 0,
            "actors": {
                "dog": {
                    "file": "dog_actor.pt",
                    "input_shape": ["B", _DOG_HISTORY_DIM],
                    "output_shape": ["B", _DOG_ACTION_DIM],
                    "graph": [
                        "latent = ELU MLP [1620, 256, 128, 25](observation_history)",
                        "action = ELU MLP [1645, 512, 256, 128, 12](concat(observation_history, latent))",
                    ],
                    "output_activation": "linear",
                },
                "arm": {
                    "file": "arm_actor.pt",
                    "input_shape": ["B", _ARM_HISTORY_DIM],
                    "output_shape": ["B", _ARM_ACTION_DIM],
                    "graph": [
                        "current_observation = observation_history[:, 580:600]",
                        "history_only = observation_history[:, 0:580]",
                        "latent = ELU MLP [600, 256, 128, 9](observation_history)",
                        "history_latent = ELU MLP [580, 512, 256, 128](history_only)",
                        "raw = ELU MLP [157, 512, 256, 128, 8](concat(current_observation, latent, history_latent))",
                        "action = concat(raw[:, 0:6], tanh(raw[:, 6:8]))",
                    ],
                },
            },
        },
        "observations": {
            "history": {
                "layout": "frame-major",
                "order": "oldest-to-newest",
                "dog": {"frames": 30, "frame_dim": 54, "flattened_dim": 1620},
                "arm": {"frames": 30, "frame_dim": 20, "flattened_dim": 600},
                "same_step_preview": "dog_actor_input = concat(committed_dog_history[:, 54:1620], preview_current_frame[:, 0:54]); the preview frame uses the same current robot state with the new arm plan.",
            },
            "dog_frame": [
                _observation_field("projected_gravity_b", 0, 3, "unitless", "R_body_to_world.T @ [0, 0, -1]"),
                _observation_field(
                    "joint_position_relative",
                    3,
                    15,
                    "rad",
                    "joint_position minus default_joint_position",
                    joint_order=list(_DOG_JOINTS),
                ),
                _observation_field(
                    "joint_velocity",
                    15,
                    27,
                    "scaled_rad_per_s",
                    "joint_velocity_rad_s * 0.05",
                    scale=0.05,
                    joint_order=list(_DOG_JOINTS),
                ),
                _observation_field(
                    "last_action", 27, 39, "policy_action", "previous raw dog action", joint_order=list(_DOG_JOINTS)
                ),
                _observation_field(
                    "dog_command",
                    39,
                    44,
                    "mixed",
                    "[vx, vy, yaw_rate, body_pitch, body_roll] * [2, 2, 0.25, 1, 1]",
                    raw_units=["m/s", "m/s", "rad/s", "rad", "rad"],
                ),
                _observation_field(
                    "arm_goal",
                    44,
                    50,
                    "mixed",
                    "hybrid-gated [radius, pitch, yaw, 0, 0, 0]",
                    raw_units=["m", "rad", "rad", "rad", "rad", "rad"],
                ),
                _observation_field("base_roll_pitch", 50, 52, "rad", "root roll and pitch"),
                _observation_field(
                    "gait_clock", 52, 54, "unitless", "[sin(2*pi*phase), cos(2*pi*phase)] at 2 Hz"
                ),
            ],
            "arm_frame": [
                _observation_field(
                    "joint_position_relative",
                    0,
                    6,
                    "rad",
                    "joint_position minus default_joint_position",
                    joint_order=list(_ARM_JOINTS),
                ),
                _observation_field(
                    "last_control_action",
                    6,
                    12,
                    "policy_action",
                    "previous raw six-dimensional arm control action; plan excluded",
                    joint_order=list(_ARM_JOINTS),
                ),
                _observation_field(
                    "arm_goal",
                    12,
                    18,
                    "mixed",
                    "hybrid-gated [radius, pitch, yaw, 0, 0, 0]",
                    raw_units=["m", "rad", "rad", "rad", "rad", "rad"],
                ),
                _observation_field("base_roll_pitch", 18, 20, "rad", "root roll and pitch"),
            ],
            "projected_gravity_xyzw_reference": {
                "quaternion_key": "root_quaternion_xyzw",
                "quaternion_semantics": "unit active rotation from robot body frame to world frame, component order [x, y, z, w]",
                "world_gravity_key": "gravity_world_unit",
                "rotation_matrix_key": "rotation_body_to_world",
                "expected_output_key": "projected_gravity_body",
                "component_formula": "[2*(w*y-x*z), -2*(y*z+w*x), 2*(x*x+y*y)-1]",
                "reference_file": "parity/dog_reference.npz",
            },
        },
        "actions_and_control": {
            "timing": {
                "physics_dt_s": 0.005,
                "physics_frequency_hz": 200.0,
                "decimation": 4,
                "policy_dt_s": 0.02,
                "policy_frequency_hz": 50.0,
            },
            "dog": {
                "joint_order": list(_DOG_JOINTS),
                "default_joint_position_rad": list(_DOG_DEFAULT_JOINT_POS),
                "target_formula": "clip(default_joint_position + 0.25 * action, -100, 100)",
                "action_scale_rad": 0.25,
                "episode_fixed_action_delay_steps": [0, 1],
            },
            "arm": {
                "output_slices": {"control": [0, 6], "plan": [6, 8]},
                "joint_order": list(_ARM_JOINTS),
                "default_joint_position_rad": list(_ARM_DEFAULT_JOINT_POS),
                "target_formula": "clip(default_joint_position + 0.25 * action[0:6], -100, 100)",
                "action_scale_rad": 0.25,
                "action_delay_steps": 0,
                "plan_order": ["body_pitch", "body_roll"],
                "actor_output_semantics": "actor output[6:8] already has tanh applied inside arm_actor.pt",
                "plan_formula": "clip(actor_output[6:8], -1, 1) * 0.4",
                "plan_limits_rad": [[-0.4, 0.4], [-0.4, 0.4]],
                "gripper_fixed_targets": {"arm_j7": 0.0, "arm_j8": 0.0},
            },
            "actuators": [
                {
                    "joints": list(_DOG_JOINTS[0:4]),
                    "effort_limit": 120.0,
                    "velocity_limit": 22.0,
                    "stiffness": 140.0,
                    "damping": 4.5,
                    "armature": 0.03,
                },
                {
                    "joints": list(_DOG_JOINTS[4:8]),
                    "effort_limit": 120.0,
                    "velocity_limit": 22.0,
                    "stiffness": 140.0,
                    "damping": 4.5,
                    "armature": 0.03,
                },
                {
                    "joints": list(_DOG_JOINTS[8:12]),
                    "effort_limit": 180.0,
                    "velocity_limit": 14.6667,
                    "stiffness": 220.0,
                    "damping": 9.0,
                    "armature": 0.03,
                },
                {
                    "joints": list(_ARM_JOINTS[0:5]),
                    "effort_limit": 100.0,
                    "velocity_limit": 5.0,
                    "stiffness": 80.0,
                    "damping": 4.0,
                    "armature": 0.0,
                },
                {
                    "joints": list(_ARM_JOINTS[5:6]),
                    "effort_limit": 100.0,
                    "velocity_limit": 3.0,
                    "stiffness": 60.0,
                    "damping": 3.0,
                    "armature": 0.0,
                },
            ],
            "source_physics": {
                "drive": "implicit joint-position PD",
                "self_collision": True,
                "merge_fixed_joints": False,
                "soft_joint_position_limit_factor": 0.9,
                "solver_position_iterations": 4,
                "solver_velocity_iterations": 0,
                "training_gain_randomization_scale": [0.8, 1.2],
            },
            "arm_tracking": {
                "goal": "[radius, pitch, yaw, 0, 0, 0]",
                "spherical_origin": "base-yaw-rotated XY offset [0.145, 0] with fixed world z=0.704",
                "end_effector_body": "arm_body6_to_gripper",
                "tcp_offset_local_m": [0.0, 0.0, 0.105],
            },
        },
        "dual_policy_protocol": {
            "stage": "hybrid enabled",
            "sequence": [
                "Run arm_actor on the 600-D arm history.",
                "Compute body pitch/roll as clip(arm_actor_output[6:8], -1, 1) * 0.4.",
                "Build the preview current dog frame from the same current robot state and the new body pitch/roll command.",
                "Build dog_actor_input as concat(committed_dog_history[:, 54:1620], preview_current_frame).",
                "Run dog_actor on the preview-aware 1620-D dog history.",
                "Commit the same arm plan, apply joint-position targets, and advance four physics steps.",
            ],
        },
        "parity": {
            "reference_format_version": _REFERENCE_FORMAT_VERSION,
            "generator": "deterministic analytic 30-frame trajectory; arm runs first and its plan constructs the dog preview frame; no random sampling",
            "dog": {
                "file": "parity/dog_reference.npz",
                "arrays": _array_schema(dog_reference),
            },
            "arm": {
                "file": "parity/arm_reference.npz",
                "arrays": _array_schema(arm_reference),
            },
        },
    }


def _yaml_scalar(value: str) -> str:
    return json.dumps(str(value), ensure_ascii=True)


def _export_versions_yaml() -> str:
    lines = [
        "bundle_format: lmp_stage2_deployment",
        f"bundle_format_version: {_BUNDLE_FORMAT_VERSION}",
        f"reference_format_version: {_REFERENCE_FORMAT_VERSION}",
        f"checkpoint_format_version: {_CHECKPOINT_FORMAT_VERSION}",
        f"checkpoint_iteration: {_CHECKPOINT_ITERATION}",
        f"python: {_yaml_scalar(platform.python_version())}",
        f"torch: {_yaml_scalar(torch.__version__)}",
        f"numpy: {_yaml_scalar(np.__version__)}",
        "torchscript_export: torch.jit.script",
        "artifacts:",
        "  dog_actor: dog_actor.pt",
        "  arm_actor: arm_actor.pt",
        "  dog_reference: parity/dog_reference.npz",
        "  arm_reference: parity/arm_reference.npz",
        "  source_contract: metadata/lmp_source_contract.json",
        "policy_manifest:",
        "  status: not_emitted",
        "  reason: downstream_deployment_schema_not_provided",
    ]
    return "\n".join(lines) + "\n"


def _verify_scripted_batch(
    eager_actor: nn.Module,
    scripted_actor: torch.jit.ScriptModule,
    reference_input: np.ndarray,
    output_dim: int,
) -> None:
    input_tensor = torch.from_numpy(reference_input)
    batch = torch.cat((input_tensor, input_tensor * 0.5, -input_tensor), dim=0)
    with torch.inference_mode():
        eager_output = eager_actor(batch)
        scripted_output = scripted_actor(batch)
    if scripted_output.shape != (3, output_dim):
        raise RuntimeError(f"Scripted actor returned unexpected batch shape {tuple(scripted_output.shape)}.")
    torch.testing.assert_close(scripted_output, eager_output, rtol=0.0, atol=0.0)


def main() -> None:
    args = _parse_args()
    log_dir, checkpoint_path = _resolve_checkpoint(args.log_dir, args.checkpoint)
    state = _load_runner_state(checkpoint_path)

    dog_actor = DogDeterministicActor()
    arm_actor = ArmDeterministicActor()
    _load_actor_parameters(dog_actor, state["dog_model"], "dog_model")
    _load_actor_parameters(arm_actor, state["arm_model"], "arm_model")

    primitives = _trajectory_primitives()
    arm_reference = _build_arm_reference(arm_actor, primitives)
    dog_reference = _build_dog_reference(dog_actor, primitives, arm_reference)

    dog_scripted = torch.jit.script(dog_actor)
    arm_scripted = torch.jit.script(arm_actor)
    _verify_scripted_batch(dog_actor, dog_scripted, dog_reference["actor_input"], _DOG_ACTION_DIM)
    _verify_scripted_batch(arm_actor, arm_scripted, arm_reference["actor_input"], _ARM_ACTION_DIM)

    source_contract = _source_contract(
        checkpoint_path,
        log_dir,
        state,
        dog_reference,
        arm_reference,
    )
    source_contract_text = json.dumps(source_contract, indent=2, ensure_ascii=False, allow_nan=False) + "\n"

    output_dir = args.output_dir.expanduser().resolve()
    parity_dir = output_dir / "parity"
    metadata_dir = output_dir / "metadata"
    parity_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(parents=True, exist_ok=True)

    dog_scripted.save(str(output_dir / "dog_actor.pt"))
    arm_scripted.save(str(output_dir / "arm_actor.pt"))
    np.savez(parity_dir / "dog_reference.npz", **dog_reference)
    np.savez(parity_dir / "arm_reference.npz", **arm_reference)
    (metadata_dir / "export_versions.yaml").write_text(_export_versions_yaml(), encoding="utf-8")
    (metadata_dir / "lmp_source_contract.json").write_text(source_contract_text, encoding="utf-8")

    print(f"Source: {checkpoint_path}")
    print(f"Dog actor: {output_dir / 'dog_actor.pt'}")
    print(f"Arm actor: {output_dir / 'arm_actor.pt'}")
    print(f"Dog parity: {parity_dir / 'dog_reference.npz'}")
    print(f"Arm parity: {parity_dir / 'arm_reference.npz'}")
    print(f"Export versions: {metadata_dir / 'export_versions.yaml'}")
    print(f"Source contract: {metadata_dir / 'lmp_source_contract.json'}")


if __name__ == "__main__":
    main()
