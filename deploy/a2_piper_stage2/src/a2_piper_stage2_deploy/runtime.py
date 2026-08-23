"""Arm-first Stage2 dual-policy logical tick runtime."""

from __future__ import annotations

from dataclasses import dataclass
import time

import numpy as np

from .actions import NamedJointTarget, PositionActionProcessor
from .contract import PolicyContract
from .observations import (
    FrameHistory,
    PolicyCommand,
    RobotSnapshot,
    build_arm_frame,
    build_dog_frame,
    next_gait_phase,
    validate_command,
    validate_snapshot,
)
from .policy import TorchScriptActors


@dataclass(frozen=True)
class RuntimeOutput:
    dog_raw_action: np.ndarray
    arm_raw_output: np.ndarray
    body_pitch_roll_plan_rad: np.ndarray
    dog_target: NamedJointTarget
    arm_target: NamedJointTarget
    inference_latency_ms: float


class DualPolicyRuntime:
    def __init__(self, contract: PolicyContract, actors: TorchScriptActors) -> None:
        self.contract = contract
        self.actors = actors
        self._dog_history = FrameHistory(contract.dog.history_frames, contract.dog.frame_dim)
        self._arm_history = FrameHistory(contract.arm.history_frames, contract.arm.frame_dim)
        self._dog_actions = PositionActionProcessor(
            contract.dog, contract.lmp_urdf_joint_limits_rad
        )
        self._arm_actions = PositionActionProcessor(
            contract.arm, contract.lmp_urdf_joint_limits_rad, control_slice=(0, 6)
        )
        self._previous_dog_raw = np.zeros(12, dtype=np.float32)
        self._previous_arm_control_raw = np.zeros(6, dtype=np.float32)
        self._committed_plan = np.zeros(2, dtype=np.float32)
        self._gait_phase = 0.0

    def reset(self) -> None:
        self._dog_history.reset()
        self._arm_history.reset()
        self._dog_actions.reset()
        self._arm_actions.reset()
        self._previous_dog_raw.fill(0.0)
        self._previous_arm_control_raw.fill(0.0)
        self._committed_plan.fill(0.0)
        self._gait_phase = 0.0

    def tick(self, snapshot: RobotSnapshot, command: PolicyCommand) -> RuntimeOutput:
        validate_snapshot(snapshot)
        validate_command(command)

        arm_frame = build_arm_frame(
            snapshot,
            command,
            self.contract.arm.default_position_rad,
            self._previous_arm_control_raw,
        )
        committed_dog_frame = build_dog_frame(
            snapshot,
            command,
            self.contract.dog.default_position_rad,
            self._previous_dog_raw,
            self._committed_plan,
            self._gait_phase,
        )
        self._arm_history.append(arm_frame)
        self._dog_history.append(committed_dog_frame)

        started = time.perf_counter_ns()
        arm_raw = self.actors.infer_arm(self._arm_history.flattened())
        plan = (np.clip(arm_raw[6:8], -1.0, 1.0) * 0.4).astype(np.float32)
        preview_dog_frame = build_dog_frame(
            snapshot,
            command,
            self.contract.dog.default_position_rad,
            self._previous_dog_raw,
            plan,
            self._gait_phase,
        )
        dog_raw = self.actors.infer_dog(self._dog_history.preview(preview_dog_frame))
        inference_latency_ms = (time.perf_counter_ns() - started) / 1_000_000.0

        dog_target = self._dog_actions.process(dog_raw)
        arm_target = self._arm_actions.process(arm_raw)
        self._previous_dog_raw = dog_raw.copy()
        self._previous_arm_control_raw = arm_raw[:6].copy()
        self._committed_plan = plan.copy()
        self._gait_phase = next_gait_phase(
            self._gait_phase,
            command,
            self.contract.gait_frequency_hz,
            self.contract.policy_period_s,
        )
        return RuntimeOutput(
            dog_raw_action=dog_raw.copy(),
            arm_raw_output=arm_raw.copy(),
            body_pitch_roll_plan_rad=plan,
            dog_target=dog_target,
            arm_target=arm_target,
            inference_latency_ms=inference_latency_ms,
        )


__all__ = [
    "DualPolicyRuntime",
    "PolicyCommand",
    "RobotSnapshot",
    "RuntimeOutput",
]
