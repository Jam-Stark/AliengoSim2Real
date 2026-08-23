"""Load the LMP-authoritative Stage2 policy contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import yaml


@dataclass(frozen=True)
class ActorContract:
    name: str
    file: str
    input_dim: int
    output_dim: int
    frame_dim: int
    history_frames: int
    joint_order: tuple[str, ...]
    default_position_rad: np.ndarray
    action_scale_rad: float
    actor_clip: tuple[float, float] | None
    processed_clip_rad: tuple[float, float]
    max_target_delta_rad: np.ndarray


@dataclass(frozen=True)
class PolicyContract:
    """Fields consumed by the deployment runtime, sourced directly from YAML."""

    path: Path
    raw: dict[str, Any]
    dog: ActorContract
    arm: ActorContract
    policy_period_s: float
    gait_frequency_hz: float
    runner_role: str
    lmp_urdf_joint_limits_rad: dict[str, tuple[float, float]]

    @classmethod
    def load(cls, path: str | Path) -> "PolicyContract":
        manifest_path = Path(path).expanduser().resolve()
        with manifest_path.open("r", encoding="utf-8") as stream:
            raw = yaml.safe_load(stream)

        if raw["schema"] != "lmp_stage2_dual_policy_source_contract":
            raise ValueError(f"Unsupported policy schema: {raw['schema']!r}")
        if raw["schema_version"] != 1:
            raise ValueError(f"Unsupported policy schema version: {raw['schema_version']!r}")
        if raw["identity"]["runner_role"] != "training_state":
            raise ValueError("Stage2 runner must remain classified as training_state")

        cls._validate_runtime_semantics(raw)

        dog = cls._actor(raw, "dog")
        arm = cls._actor(raw, "arm")
        if dog.input_dim != dog.frame_dim * dog.history_frames:
            raise ValueError("Dog history dimensions do not match actor input")
        if arm.input_dim != arm.frame_dim * arm.history_frames:
            raise ValueError("Arm history dimensions do not match actor input")
        if (dog.frame_dim, dog.history_frames, dog.output_dim) != (54, 30, 12):
            raise ValueError("Unexpected dog Stage2 dimensions")
        if (arm.frame_dim, arm.history_frames, arm.output_dim) != (20, 30, 8):
            raise ValueError("Unexpected arm Stage2 dimensions")

        joint_limits = {
            entry["name"]: (float(entry["lower"]), float(entry["upper"]))
            for entry in raw["joint_limits"]["entries"]
            if entry["type"] == "revolute"
        }
        return cls(
            path=manifest_path,
            raw=raw,
            dog=dog,
            arm=arm,
            policy_period_s=float(raw["timing"]["policy_period_s"]),
            gait_frequency_hz=float(raw["commands"]["gait"]["frequency_hz"]),
            runner_role=raw["identity"]["runner_role"],
            lmp_urdf_joint_limits_rad=joint_limits,
        )

    @staticmethod
    def _validate_runtime_semantics(raw: dict[str, Any]) -> None:
        numeric = raw["numeric"]
        if (
            numeric["dtype"] != "float32"
            or numeric["observation_normalization"] != "none"
            or numeric["observation_clip"] != "none"
            or numeric["observation_corruption"] != "disabled"
        ):
            raise ValueError("Unsupported Stage2 numeric observation semantics")
        history = raw["history"]
        if history["layout"] != "frame_major" or history["flatten_order"] != "oldest_to_newest":
            raise ValueError("Stage2 history must be frame-major oldest-to-newest")
        if history["reset"]["rule"] != "repeat_first_valid_post_reset_frame_30_times":
            raise ValueError("Unsupported Stage2 history reset rule")
        if not history["reset"]["all_zero_flat_history_is_invalid"]:
            raise ValueError("All-zero Stage2 history must remain invalid")
        if raw["inference_protocol"]["preview_mutates_persistent_history"]:
            raise ValueError("Dog same-tick preview must not mutate persistent history")

        timing = raw["timing"]
        expected_period = float(timing["training_sim_dt_s"]) * int(timing["training_decimation"])
        if abs(expected_period - float(timing["policy_period_s"])) > 1.0e-12:
            raise ValueError("sim.dt * decimation does not match the policy period")

        for actor_name in ("dog", "arm"):
            model = raw["models"][actor_name]
            action = raw["actions"][actor_name]
            history_shape = raw["history"][actor_name]
            observation = raw["observations"][actor_name]
            if model["format"] != "torchscript" or model["method"] != "forward":
                raise ValueError(f"{actor_name} model must use TorchScript forward")
            if int(model["output_shape"][1]) != int(action["actor_output_dim"]):
                raise ValueError(f"{actor_name} model and action output dimensions differ")
            if int(observation["frame_dim"]) != int(history_shape["frame_dim"]):
                raise ValueError(f"{actor_name} observation and history frame dimensions differ")
            _validate_blocks(actor_name, observation["blocks"], int(observation["frame_dim"]))
            if action["mode"] != "position_offset":
                raise ValueError(f"{actor_name} Stage2 action must use position_offset")
            joint_count = len(action["joint_order"])
            if len(action["default_joint_position_rad"]) != joint_count:
                raise ValueError(f"{actor_name} default position length differs from joint order")
            if len(action["max_target_delta_per_policy_tick_rad"]) != joint_count:
                raise ValueError(f"{actor_name} rate-limit length differs from joint order")

        dog_order = raw["actions"]["dog"]["joint_order"]
        dog_blocks = {block["name"]: block for block in raw["observations"]["dog"]["blocks"]}
        if dog_blocks["leg_joint_position_relative"]["joint_order"] != dog_order:
            raise ValueError("Dog position observation and action joint orders differ")
        if dog_blocks["leg_joint_velocity_scaled"]["joint_order"] != dog_order:
            raise ValueError("Dog velocity observation and action joint orders differ")
        arm_order = raw["actions"]["arm"]["joint_order"]
        arm_blocks = {block["name"]: block for block in raw["observations"]["arm"]["blocks"]}
        if arm_blocks["arm_joint_position_relative"]["joint_order"] != arm_order:
            raise ValueError("Arm observation and action joint orders differ")
        if raw["actions"]["arm"]["control_slice"] != [0, 6]:
            raise ValueError("Arm control slice must be [0, 6]")
        if raw["actions"]["arm"]["plan_slice"] != [6, 8]:
            raise ValueError("Arm plan slice must be [6, 8]")
        if raw["actions"]["gripper"]["actor_outputs"] != "none":
            raise ValueError("Stage2 actor must not synthesize a gripper command")

    @staticmethod
    def _actor(raw: dict[str, Any], name: str) -> ActorContract:
        model = raw["models"][name]
        history = raw["history"][name]
        action = raw["actions"][name]
        output_dim = int(action["actor_output_dim"])
        joint_order = tuple(action["joint_order"])
        actor_clip = action["actor_clip"]
        return ActorContract(
            name=name,
            file=model["file"],
            input_dim=int(model["canonical_probe_shape"][1]),
            output_dim=output_dim,
            frame_dim=int(history["frame_dim"]),
            history_frames=int(history["frames"]),
            joint_order=joint_order,
            default_position_rad=np.asarray(action["default_joint_position_rad"], dtype=np.float32),
            action_scale_rad=float(action["scale_rad"]),
            actor_clip=None if actor_clip is None else (float(actor_clip[0]), float(actor_clip[1])),
            processed_clip_rad=(
                float(action["processed_target_clip_rad"][0]),
                float(action["processed_target_clip_rad"][1]),
            ),
            max_target_delta_rad=np.asarray(
                action["max_target_delta_per_policy_tick_rad"], dtype=np.float32
            ),
        )

    @property
    def bundle_dir(self) -> Path:
        return self.path.parent


def _validate_blocks(actor_name: str, blocks: list[dict[str, Any]], frame_dim: int) -> None:
    cursor = 0
    for block in blocks:
        start, stop = block["slice"]
        if start != cursor or stop - start != block["dim"]:
            raise ValueError(f"{actor_name} observation block {block['name']} has an invalid slice")
        cursor = stop
    if cursor != frame_dim:
        raise ValueError(f"{actor_name} observation blocks do not cover the frame")
