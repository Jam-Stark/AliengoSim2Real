from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

JOINT_NAMES = tuple(f"arm_j{index}" for index in range(1, 7))
JOINT_LIMITS_DEG = (
    (-150.0, 150.0),
    (0.0, 180.0),
    (-170.0, 0.0),
    (-100.0, 100.0),
    (-70.0, 70.0),
    (-120.0, 120.0),
)
JOINT_LIMITS_RAD = tuple(
    (math.radians(lower), math.radians(upper))
    for lower, upper in JOINT_LIMITS_DEG
)


class CommandValidationError(ValueError):
    """Raised when a network joint command cannot be sent to PiPER."""


@dataclass(frozen=True)
class PiperFeedback:
    positions_rad: tuple[float, ...]
    velocities_rad_s: tuple[float, ...]
    efforts_nm: tuple[float, ...]
    sdk_timestamp_s: float
    joint_hz: float
    status_hz: float
    arm_status: int
    ctrl_mode: int
    speed_sample_count: tuple[int, ...]

    @property
    def healthy(self) -> bool:
        return self.joint_hz > 0.0 and self.status_hz > 0.0 and self.arm_status == 0


def millidegrees_to_radians(values: Sequence[int | float]) -> tuple[float, ...]:
    return tuple(float(value) * math.pi / 180000.0 for value in values)


def radians_to_millidegrees(values: Sequence[float]) -> tuple[int, ...]:
    return tuple(round(math.degrees(float(value)) * 1000.0) for value in values)


def order_joint_values(
    names: Sequence[str], values: Sequence[float], field_name: str
) -> tuple[float, ...]:
    if len(names) != len(JOINT_NAMES) or len(values) != len(JOINT_NAMES):
        raise CommandValidationError(
            f"expected {len(JOINT_NAMES)} joint names and {field_name} values"
        )
    if len(set(names)) != len(names):
        raise CommandValidationError("joint names must be unique")
    if set(names) != set(JOINT_NAMES):
        raise CommandValidationError(
            f"joint names must be exactly {list(JOINT_NAMES)}"
        )

    by_name = {name: float(value) for name, value in zip(names, values)}
    ordered = tuple(by_name[name] for name in JOINT_NAMES)
    for index, value in enumerate(ordered, start=1):
        if not math.isfinite(value):
            raise CommandValidationError(
                f"{JOINT_NAMES[index - 1]} {field_name} is not finite"
            )
    return ordered


def normalize_joint_command(
    names: Sequence[str], positions: Sequence[float]
) -> tuple[float, ...]:
    ordered = order_joint_values(names, positions, "position")
    for index, (position, limits) in enumerate(
        zip(ordered, JOINT_LIMITS_RAD), start=1
    ):
        lower, upper = limits
        if not lower <= position <= upper:
            raise CommandValidationError(
                f"{JOINT_NAMES[index - 1]}={position:.6f} rad is outside "
                f"[{lower:.6f}, {upper:.6f}]"
            )
    return ordered
