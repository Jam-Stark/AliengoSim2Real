"""Observation assembly for the real Stage2 LMP contract."""

from __future__ import annotations

from dataclasses import dataclass
from math import asin, atan2, cos, pi, sin

import numpy as np


@dataclass(frozen=True)
class RobotSnapshot:
    monotonic_time_s: float
    root_quaternion_xyzw: np.ndarray
    dog_joint_position_rad: np.ndarray
    dog_joint_velocity_rad_s: np.ndarray
    arm_joint_position_rad: np.ndarray


@dataclass(frozen=True)
class PolicyCommand:
    locomotion_vx_vy_yaw: np.ndarray
    arm_goal_radius_pitch_yaw: np.ndarray


class FrameHistory:
    def __init__(self, frames: int, frame_dim: int) -> None:
        self._frames = frames
        self._frame_dim = frame_dim
        self._data: np.ndarray | None = None

    def reset(self) -> None:
        self._data = None

    def append(self, frame: np.ndarray) -> None:
        checked = _vector("history frame", frame, self._frame_dim)
        if self._data is None:
            self._data = np.repeat(checked[None, :], self._frames, axis=0)
        else:
            self._data = np.concatenate((self._data[1:], checked[None, :]), axis=0)

    def flattened(self) -> np.ndarray:
        if self._data is None:
            raise RuntimeError("History has not received its first valid frame")
        return self._data.reshape(1, self._frames * self._frame_dim)

    def preview(self, frame: np.ndarray) -> np.ndarray:
        if self._data is None:
            raise RuntimeError("History has not received its first valid frame")
        checked = _vector("preview frame", frame, self._frame_dim)
        return np.concatenate((self._data[1:], checked[None, :]), axis=0).reshape(1, -1)


def validate_snapshot(snapshot: RobotSnapshot) -> RobotSnapshot:
    _vector("root quaternion", snapshot.root_quaternion_xyzw, 4)
    _vector("dog joint position", snapshot.dog_joint_position_rad, 12)
    _vector("dog joint velocity", snapshot.dog_joint_velocity_rad_s, 12)
    _vector("arm joint position", snapshot.arm_joint_position_rad, 6)
    return snapshot


def validate_command(command: PolicyCommand) -> PolicyCommand:
    _vector("locomotion command", command.locomotion_vx_vy_yaw, 3)
    _vector("arm goal", command.arm_goal_radius_pitch_yaw, 3)
    return command


def projected_gravity_body(quaternion_xyzw: np.ndarray) -> np.ndarray:
    x, y, z, w = _normalized_quaternion(quaternion_xyzw)
    return np.asarray(
        [2.0 * (w * y - x * z), -2.0 * (y * z + w * x), 2.0 * (x * x + y * y) - 1.0],
        dtype=np.float32,
    )


def base_roll_pitch(quaternion_xyzw: np.ndarray) -> np.ndarray:
    x, y, z, w = _normalized_quaternion(quaternion_xyzw)
    roll = atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y))
    pitch_argument = float(np.clip(2.0 * (w * y - z * x), -1.0, 1.0))
    return np.asarray([roll, asin(pitch_argument)], dtype=np.float32)


def gait_clock(phase: float) -> np.ndarray:
    return np.asarray([sin(2.0 * pi * phase), cos(2.0 * pi * phase)], dtype=np.float32)


def build_arm_frame(
    snapshot: RobotSnapshot,
    command: PolicyCommand,
    default_joint_position_rad: np.ndarray,
    previous_raw_control_action: np.ndarray,
) -> np.ndarray:
    return np.concatenate(
        (
            _vector("arm joint position", snapshot.arm_joint_position_rad, 6)
            - _vector("arm default position", default_joint_position_rad, 6),
            _vector("previous raw arm action", previous_raw_control_action, 6),
            _arm_goal_lpy6(command),
            base_roll_pitch(snapshot.root_quaternion_xyzw),
        )
    ).astype(np.float32)


def build_dog_frame(
    snapshot: RobotSnapshot,
    command: PolicyCommand,
    default_joint_position_rad: np.ndarray,
    previous_raw_action: np.ndarray,
    body_pitch_roll_plan_rad: np.ndarray,
    gait_phase: float,
) -> np.ndarray:
    locomotion = _vector("locomotion command", command.locomotion_vx_vy_yaw, 3)
    plan = _vector("body pitch/roll plan", body_pitch_roll_plan_rad, 2)
    dog_command = np.concatenate((locomotion, plan)) * np.asarray(
        [2.0, 2.0, 0.25, 1.0, 1.0], dtype=np.float32
    )
    return np.concatenate(
        (
            projected_gravity_body(snapshot.root_quaternion_xyzw),
            _vector("dog joint position", snapshot.dog_joint_position_rad, 12)
            - _vector("dog default position", default_joint_position_rad, 12),
            _vector("dog joint velocity", snapshot.dog_joint_velocity_rad_s, 12) * 0.05,
            _vector("previous raw dog action", previous_raw_action, 12),
            dog_command,
            _arm_goal_lpy6(command),
            base_roll_pitch(snapshot.root_quaternion_xyzw),
            gait_clock(gait_phase),
        )
    ).astype(np.float32)


def next_gait_phase(phase: float, command: PolicyCommand, frequency_hz: float, period_s: float) -> float:
    locomotion = _vector("locomotion command", command.locomotion_vx_vy_yaw, 3)
    if np.all(locomotion == 0.0):
        return 0.0
    return (phase + frequency_hz * period_s) % 1.0


def _arm_goal_lpy6(command: PolicyCommand) -> np.ndarray:
    return np.concatenate(
        (
            _vector("arm goal", command.arm_goal_radius_pitch_yaw, 3),
            np.zeros(3, dtype=np.float32),
        )
    )


def _normalized_quaternion(value: np.ndarray) -> np.ndarray:
    quaternion = _vector("root quaternion", value, 4)
    norm = float(np.linalg.norm(quaternion))
    if norm == 0.0:
        raise ValueError("Root quaternion has zero norm")
    return quaternion / norm


def _vector(name: str, value: np.ndarray, size: int) -> np.ndarray:
    array = np.asarray(value, dtype=np.float32)
    if array.shape != (size,):
        raise ValueError(f"{name} must have shape ({size},), got {array.shape}")
    if not np.isfinite(array).all():
        raise ValueError(f"{name} contains a non-finite value")
    return array
