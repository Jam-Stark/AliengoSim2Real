"""Named joint target processing for Stage2 actor outputs."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .contract import ActorContract


@dataclass(frozen=True)
class NamedJointTarget:
    joint_names: tuple[str, ...]
    positions_rad: np.ndarray


class PositionActionProcessor:
    def __init__(
        self,
        contract: ActorContract,
        joint_limits_rad: dict[str, tuple[float, float]],
        control_slice: tuple[int, int] | None = None,
    ) -> None:
        self._contract = contract
        self._limits = joint_limits_rad
        self._control_slice = control_slice
        self._previous_target = contract.default_position_rad.copy()

    def reset(self) -> None:
        self._previous_target = self._contract.default_position_rad.copy()

    def process(self, raw_actor_output: np.ndarray) -> NamedJointTarget:
        raw = np.asarray(raw_actor_output, dtype=np.float32)
        if raw.shape != (self._contract.output_dim,):
            raise ValueError(
                f"{self._contract.name} actor output must have shape "
                f"({self._contract.output_dim},), got {raw.shape}"
            )
        if not np.isfinite(raw).all():
            raise ValueError(f"{self._contract.name} actor output contains a non-finite value")
        control = raw if self._control_slice is None else raw[slice(*self._control_slice)]
        if self._contract.actor_clip is not None:
            control = np.clip(control, *self._contract.actor_clip)
        target = self._contract.default_position_rad + self._contract.action_scale_rad * control
        target = np.clip(target, *self._contract.processed_clip_rad)
        lower = np.asarray([self._limits[name][0] for name in self._contract.joint_order], dtype=np.float32)
        upper = np.asarray([self._limits[name][1] for name in self._contract.joint_order], dtype=np.float32)
        target = np.clip(target, lower, upper)
        delta = np.clip(
            target - self._previous_target,
            -self._contract.max_target_delta_rad,
            self._contract.max_target_delta_rad,
        )
        target = (self._previous_target + delta).astype(np.float32)
        self._previous_target = target
        return NamedJointTarget(self._contract.joint_order, target.copy())
