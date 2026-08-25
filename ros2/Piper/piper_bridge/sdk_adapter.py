from __future__ import annotations

import math
import time
from typing import Any

from .model import PiperFeedback, millidegrees_to_radians, radians_to_millidegrees


class PiperSdkError(RuntimeError):
    """Raised when the local PiPER SDK/CAN boundary is not usable."""


class PiperSdkAdapter:
    """Single-owner wrapper around the tested ``C_PiperInterface_V2`` path."""

    def __init__(
        self,
        can_name: str,
        control_period_s: float,
    ) -> None:
        self.can_name = can_name
        self.control_period_s = control_period_s
        self._piper: Any | None = None
        self._previous_joint_timestamp_s: float | None = None

    @property
    def connected(self) -> bool:
        return self._piper is not None

    def connect(self) -> None:
        if self._piper is not None:
            return
        try:
            from piper_sdk import C_PiperInterface_V2
        except ImportError as exc:
            raise PiperSdkError(
                "piper_sdk is not installed; install the tested krushell/piper_sdk fork"
            ) from exc

        piper = C_PiperInterface_V2(self.can_name)
        if not hasattr(piper, "GetArmHighSpdInfoAverage"):
            raise PiperSdkError(
                "installed piper_sdk lacks GetArmHighSpdInfoAverage; "
                "the bridge requires the tested krushell fork"
            )
        try:
            piper.ConnectPort()
        except Exception:
            try:
                piper.DisconnectPort()
            finally:
                raise
        self._piper = piper

    def disconnect(self) -> None:
        if self._piper is None:
            return
        try:
            self._piper.DisconnectPort()
        finally:
            self._piper = None
            self._previous_joint_timestamp_s = None

    def wait_for_feedback(self, timeout_s: float) -> PiperFeedback:
        deadline = time.monotonic() + timeout_s
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                feedback = self.read_feedback(require_speed_samples=False)
                if feedback.joint_hz > 0.0 and feedback.status_hz > 0.0:
                    return feedback
            except Exception as exc:  # SDK startup can be incomplete for a few frames.
                last_error = exc
            time.sleep(0.02)
        detail = f": {last_error}" if last_error is not None else ""
        raise PiperSdkError(
            f"no complete PiPER feedback within {timeout_s:.1f}s{detail}"
        )

    def read_feedback(self, require_speed_samples: bool = True) -> PiperFeedback:
        piper = self._require_connected()
        joint_msg = piper.GetArmJointMsgs()
        status_msg = piper.GetArmStatus()

        joint_state = joint_msg.joint_state
        positions = millidegrees_to_radians(
            (
                joint_state.joint_1,
                joint_state.joint_2,
                joint_state.joint_3,
                joint_state.joint_4,
                joint_state.joint_5,
                joint_state.joint_6,
            )
        )

        window_end = float(joint_msg.time_stamp)
        if not math.isfinite(window_end) or window_end <= 0.0:
            window_end = time.time()
        previous = self._previous_joint_timestamp_s
        if previous is None:
            window_start = window_end - self.control_period_s
        else:
            elapsed = window_end - previous
            if 0.5 * self.control_period_s <= elapsed <= 2.0 * self.control_period_s:
                window_start = previous
            else:
                window_start = window_end - self.control_period_s

        averaged = piper.GetArmHighSpdInfoAverage(window_start, window_end)
        self._previous_joint_timestamp_s = window_end
        counts = tuple(int(value) for value in averaged.sample_count)
        if require_speed_samples and any(value == 0 for value in counts):
            missing = [index for index, value in enumerate(counts, start=1) if value == 0]
            raise PiperSdkError(
                f"missing high-speed motor feedback in the control window: joints={missing}"
            )

        velocities = tuple(float(value) * 0.001 for value in averaged.motor_speed)
        latest_motors = tuple(
            getattr(averaged.latest, f"motor_{index}") for index in range(1, 7)
        )
        efforts = tuple(float(motor.effort) * 0.001 for motor in latest_motors)
        arm_status = status_msg.arm_status
        return PiperFeedback(
            positions_rad=positions,
            velocities_rad_s=velocities,
            efforts_nm=efforts,
            sdk_timestamp_s=window_end,
            joint_hz=float(joint_msg.Hz),
            status_hz=float(status_msg.Hz),
            arm_status=int(arm_status.arm_status),
            ctrl_mode=int(arm_status.ctrl_mode),
            speed_sample_count=counts,
        )

    def read_joint_limits(
        self, timeout_s: float = 2.0
    ) -> tuple[tuple[tuple[float, float], ...], tuple[float, ...]]:
        piper = self._require_connected()
        piper.SearchAllMotorMaxAngleSpd()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            motors = (
                piper.GetAllMotorAngleLimitMaxSpd()
                .all_motor_angle_limit_max_spd.motor
            )
            limits = tuple(
                (
                    math.radians(float(motors[index].min_angle_limit) * 0.1),
                    math.radians(float(motors[index].max_angle_limit) * 0.1),
                )
                for index in range(1, 7)
            )
            speeds = tuple(
                float(motors[index].max_joint_spd) * 0.001
                for index in range(1, 7)
            )
            if all(lower < upper for lower, upper in limits) and all(
                speed > 0.0 for speed in speeds
            ):
                return limits, speeds
            time.sleep(0.02)
        raise PiperSdkError(
            f"no complete PiPER motor limit feedback within {timeout_s:.1f}s"
        )

    def enable(self, timeout_s: float) -> bool:
        piper = self._require_connected()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if bool(piper.EnablePiper()):
                return True
            time.sleep(0.01)
        return False

    def prepare_joint_control(self, timeout_s: float) -> PiperFeedback:
        piper = self._require_connected()
        deadline = time.monotonic() + timeout_s
        stable_since: float | None = None
        last_timestamp: float | None = None
        last_feedback: PiperFeedback | None = None
        while time.monotonic() < deadline:
            piper.MotionCtrl_2(0x01, 0x01, 0, 0xAD)
            status_msg = piper.GetArmStatus()
            timestamp = float(status_msg.time_stamp)
            if float(status_msg.Hz) <= 0.0 or timestamp == last_timestamp:
                time.sleep(0.005)
                continue
            last_timestamp = timestamp
            arm_status = int(status_msg.arm_status.arm_status)
            ctrl_mode = int(status_msg.arm_status.ctrl_mode)
            if arm_status == 0 and ctrl_mode == 0x01:
                if stable_since is None:
                    stable_since = time.monotonic()
                if time.monotonic() - stable_since >= 0.5:
                    return self.read_feedback(require_speed_samples=False)
            else:
                stable_since = None
                if arm_status not in (0, 0x05):
                    raise PiperSdkError(
                        "PiPER mode switch failed: "
                        f"arm_status={arm_status}, ctrl_mode={ctrl_mode}"
                    )
            time.sleep(0.005)
        if last_feedback is None:
            last_feedback = self.read_feedback(require_speed_samples=False)
        raise PiperSdkError(
            "PiPER CAN/MIT-high-follow mode did not stabilize: "
            f"arm_status={last_feedback.arm_status}, "
            f"ctrl_mode={last_feedback.ctrl_mode}"
        )

    def disable(self, timeout_s: float) -> bool:
        piper = self._require_connected()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if not bool(piper.DisablePiper()):
                return True
            time.sleep(0.01)
        return False

    def resume(self, timeout_s: float) -> PiperFeedback:
        piper = self._require_connected()
        send_errors: list[Exception] = []
        success_count = 0
        for _ in range(5):
            try:
                piper.MotionCtrl_1(0x02, 0, 0)
                success_count += 1
            except Exception as exc:
                send_errors.append(exc)
            time.sleep(0.01)
        if success_count == 0:
            raise PiperSdkError(f"PiPER resume command failed: {send_errors[-1]}")

        deadline = time.monotonic() + timeout_s
        last_feedback: PiperFeedback | None = None
        while time.monotonic() < deadline:
            last_feedback = self.read_feedback(require_speed_samples=False)
            if (
                last_feedback.joint_hz > 0.0
                and last_feedback.status_hz > 0.0
                and last_feedback.arm_status == 0
            ):
                return last_feedback
            time.sleep(0.02)
        last_status = last_feedback.arm_status if last_feedback is not None else -1
        raise PiperSdkError(
            f"PiPER did not resume within {timeout_s:.1f}s; arm_status={last_status}"
        )

    def command_joint_positions(self, positions_rad: tuple[float, ...]) -> None:
        piper = self._require_connected()
        target = radians_to_millidegrees(positions_rad)
        # Match krushell/piper_sdk's piper_set_mit.py: CAN control, MOVE J,
        # zero trajectory speed, and the 0xAD MIT/high-follow flag.  JointCtrl
        # remains the absolute-position command; this is not JointMitCtrl.
        piper.MotionCtrl_2(0x01, 0x01, 0, 0xAD)
        piper.JointCtrl(*target)

    def quick_stop(self) -> None:
        piper = self._require_connected()
        errors: list[Exception] = []
        success_count = 0
        for _ in range(5):
            try:
                piper.MotionCtrl_1(0x01, 0, 0)
                success_count += 1
            except Exception as exc:
                errors.append(exc)
            time.sleep(0.01)
        if success_count == 0:
            raise PiperSdkError(f"PiPER quick stop failed: {errors[-1]}")

    def _require_connected(self) -> Any:
        if self._piper is None:
            raise PiperSdkError("PiPER SDK is not connected")
        return self._piper
