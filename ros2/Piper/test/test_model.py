import math

import pytest

from piper_bridge.model import (
    CommandValidationError,
    JOINT_NAMES,
    millidegrees_to_radians,
    normalize_joint_command,
    radians_to_millidegrees,
)


def test_joint_command_is_reordered_by_name() -> None:
    names = tuple(reversed(JOINT_NAMES))
    positions = (0.5, 0.4, 0.3, -0.2, 0.1, 0.0)
    ordered = normalize_joint_command(names, positions)
    assert ordered == tuple(reversed(positions))


def test_joint_limit_is_enforced() -> None:
    with pytest.raises(CommandValidationError, match="arm_j2"):
        normalize_joint_command(JOINT_NAMES, (0.0, -0.01, 0.0, 0.0, 0.0, 0.0))


def test_native_position_round_trip() -> None:
    native = (0, 45000, -90000, 1000, -2000, 3000)
    radians = millidegrees_to_radians(native)
    assert radians_to_millidegrees(radians) == native
    assert radians[1] == pytest.approx(math.pi / 4.0)
