from __future__ import annotations

import math
import time
from types import SimpleNamespace
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from .client import PiperBridgeClient


class PiperSdkRos2Facade:
    """Subset of ``C_PiperInterface_V2`` used by the tested manipulation task.

    The facade is only a laptop-side test adapter. CAN ownership, command
    watchdogs, enable and stop remain on A2 PC2 inside ``piper_bridge``.
    """

    def __init__(
        self,
        client: "PiperBridgeClient",
        feedback_timeout_s: float = 0.5,
        speed_sample_wait_s: float = 0.04,
    ) -> None:
        self.client = client
        self.feedback_timeout_s = feedback_timeout_s
        self.speed_sample_wait_s = speed_sample_wait_s
        self.connected = False
        self.enabled = False
        self.requested_speed_percent = 0
        self._last_speed_state_monotonic_s: float | None = None
        self._enabled_monotonic_s: float | None = None

    def ConnectPort(self) -> None:
        self.client.wait_for_state(3.0)
        self.client.wait_for_diagnostics(3.0)
        self.connected = True

    def DisconnectPort(self) -> None:
        self.connected = False

    def EnablePiper(self) -> bool:
        success, _message = self.client.enable()
        self.enabled = success
        self._enabled_monotonic_s = time.monotonic() if success else None
        return success

    def DisablePiper(self) -> bool:
        success, _message = self.client.disable()
        self.enabled = False
        self._enabled_monotonic_s = None
        return success

    def MotionCtrl_2(
        self, ctrl_mode: int, move_mode: int, speed_percent: int, mit_mode: int
    ) -> None:
        if (int(ctrl_mode), int(move_mode), int(mit_mode)) != (0x01, 0x01, 0x00):
            raise RuntimeError(
                "remote facade supports only CAN control, MOVE J, position-speed mode"
            )
        self.requested_speed_percent = int(speed_percent)
        diagnostics = self.client.latest_diagnostics
        if diagnostics is not None:
            configured = int(diagnostics.values.get("speed_percent", speed_percent))
            if configured != self.requested_speed_percent:
                raise RuntimeError(
                    f"bridge speed_percent={configured} does not match "
                    f"task request={self.requested_speed_percent}"
                )

    def JointCtrl(self, *target_millidegrees: int) -> None:
        if len(target_millidegrees) != 6:
            raise ValueError("JointCtrl requires six joint targets")
        positions = tuple(
            math.radians(float(value) / 1000.0)
            for value in target_millidegrees
        )
        self.client.publish_joint_positions(positions)

    def MotionCtrl_1(
        self, emergency_stop: int, track_ctrl: int, drag_teach: int
    ) -> None:
        if (int(emergency_stop), int(track_ctrl), int(drag_teach)) != (0x01, 0, 0):
            raise RuntimeError("remote facade supports only the tested quick-stop command")
        if not self.enabled:
            return
        success, message = self.client.stop()
        self.enabled = False
        self._enabled_monotonic_s = None
        if not success:
            raise RuntimeError(message)

    def GetArmJointMsgs(self):
        self.client.pump(0.0)
        state = self.client.latest_state
        if state is None:
            return self._empty_joint_message()
        fresh = self._state_is_fresh(state)
        native_positions = tuple(
            round(math.degrees(value) * 1000.0) for value in state.positions_rad
        )
        joint_state = SimpleNamespace(
            **{
                f"joint_{index}": native_positions[index - 1]
                for index in range(1, 7)
            }
        )
        return SimpleNamespace(
            time_stamp=state.received_wall_time_s,
            Hz=state.measured_hz if fresh else 0.0,
            joint_state=joint_state,
        )

    def GetArmStatus(self):
        self.client.pump(0.0)
        diagnostics = self.client.latest_diagnostics
        if diagnostics is None or not self.client.diagnostics_are_fresh(
            self.feedback_timeout_s
        ):
            return self._empty_status_message()
        arm_status = int(diagnostics.values.get("arm_status", "-1"))
        ctrl_mode = int(diagnostics.values.get("ctrl_mode", "-1"))
        status_hz = float(diagnostics.values.get("status_hz", "0"))
        bridge_enabled = diagnostics.values.get("enabled", "false") == "true"
        enabled_grace_elapsed = (
            self._enabled_monotonic_s is not None
            and time.monotonic() - self._enabled_monotonic_s > 0.3
        )
        if self.enabled and enabled_grace_elapsed and not bridge_enabled:
            status_hz = 0.0
        return SimpleNamespace(
            time_stamp=time.time(),
            Hz=status_hz if arm_status >= 0 else 0.0,
            arm_status=SimpleNamespace(
                arm_status=arm_status,
                ctrl_mode=ctrl_mode,
            ),
        )

    def GetArmHighSpdInfoAverage(self, start_time: float, end_time: float):
        self.client.pump(0.0)
        state = self.client.latest_state
        deadline = time.monotonic() + self.speed_sample_wait_s
        while (
            state is not None
            and state.received_monotonic_s == self._last_speed_state_monotonic_s
            and time.monotonic() < deadline
        ):
            self.client.pump(min(0.005, deadline - time.monotonic()))
            state = self.client.latest_state
        state_is_new = (
            state is not None
            and state.received_monotonic_s != self._last_speed_state_monotonic_s
        )
        if state is None or not self._state_is_fresh(state) or not state_is_new:
            speeds_native = (0,) * 6
            sample_count = (0,) * 6
        else:
            speeds_native = tuple(
                round(value * 1000.0) for value in state.velocities_rad_s
            )
            sample_count = (1,) * 6
            self._last_speed_state_monotonic_s = state.received_monotonic_s
        latest = SimpleNamespace(
            **{
                f"motor_{index}": SimpleNamespace(
                    motor_speed=speeds_native[index - 1],
                    effort=(
                        round(state.efforts_nm[index - 1] * 1000.0)
                        if state is not None
                        else 0
                    ),
                )
                for index in range(1, 7)
            }
        )
        return SimpleNamespace(
            start_time=float(start_time),
            end_time=float(end_time),
            motor_speed=speeds_native,
            sample_count=sample_count,
            latest=latest,
        )

    def _state_is_fresh(self, state: Any) -> bool:
        return time.monotonic() - state.received_monotonic_s <= self.feedback_timeout_s

    @staticmethod
    def _empty_joint_message():
        return SimpleNamespace(
            time_stamp=0.0,
            Hz=0.0,
            joint_state=SimpleNamespace(
                joint_1=0,
                joint_2=0,
                joint_3=0,
                joint_4=0,
                joint_5=0,
                joint_6=0,
            ),
        )

    @staticmethod
    def _empty_status_message():
        return SimpleNamespace(
            time_stamp=0.0,
            Hz=0.0,
            arm_status=SimpleNamespace(arm_status=-1, ctrl_mode=-1),
        )
