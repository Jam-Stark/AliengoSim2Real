from __future__ import annotations

import math
import time

import rclpy
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
from rclpy.node import Node
from rclpy.qos import (
    DurabilityPolicy,
    HistoryPolicy,
    QoSProfile,
    ReliabilityPolicy,
    qos_profile_sensor_data,
)
from sensor_msgs.msg import JointState
from std_srvs.srv import Trigger
from trajectory_msgs.msg import JointTrajectory

from .model import CommandValidationError, JOINT_NAMES, PiperFeedback, normalize_joint_command
from .sdk_adapter import PiperSdkAdapter, PiperSdkError


class PiperBridgeNode(Node):
    """Own PiPER CAN on PC2 and expose a small latest-value ROS 2 interface."""

    def __init__(self) -> None:
        super().__init__("piper_bridge")
        self.declare_parameter("can_name", "can0")
        self.declare_parameter("control_rate_hz", 50.0)
        self.declare_parameter("diagnostic_rate_hz", 10.0)
        self.declare_parameter("command_timeout_s", 0.20)
        self.declare_parameter("feedback_timeout_s", 0.50)
        self.declare_parameter("startup_feedback_timeout_s", 3.0)
        self.declare_parameter("enable_timeout_s", 5.0)
        self.declare_parameter("disable_timeout_s", 5.0)
        self.declare_parameter("resume_timeout_s", 3.0)

        self.can_name = str(self.get_parameter("can_name").value)
        self.control_rate_hz = float(self.get_parameter("control_rate_hz").value)
        self.diagnostic_rate_hz = float(
            self.get_parameter("diagnostic_rate_hz").value
        )
        self.command_timeout_s = float(
            self.get_parameter("command_timeout_s").value
        )
        self.feedback_timeout_s = float(
            self.get_parameter("feedback_timeout_s").value
        )
        startup_feedback_timeout_s = float(
            self.get_parameter("startup_feedback_timeout_s").value
        )
        self.enable_timeout_s = float(
            self.get_parameter("enable_timeout_s").value
        )
        self.disable_timeout_s = float(
            self.get_parameter("disable_timeout_s").value
        )
        self.resume_timeout_s = float(
            self.get_parameter("resume_timeout_s").value
        )
        if self.control_rate_hz <= 0.0 or self.diagnostic_rate_hz <= 0.0:
            raise ValueError("control and diagnostic rates must be positive")
        if min(
            self.command_timeout_s,
            self.feedback_timeout_s,
            startup_feedback_timeout_s,
            self.enable_timeout_s,
            self.disable_timeout_s,
            self.resume_timeout_s,
        ) <= 0.0:
            raise ValueError("all timeout parameters must be positive")
        command_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
        )
        diagnostics_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
        )
        self._state_publisher = self.create_publisher(
            JointState, "joint_states", qos_profile_sensor_data
        )
        self._diagnostics_publisher = self.create_publisher(
            DiagnosticArray, "diagnostics", diagnostics_qos
        )
        self.create_subscription(
            JointTrajectory, "joint_command", self._command_callback, command_qos
        )
        self.create_service(Trigger, "enable", self._enable_callback)
        self.create_service(Trigger, "resume", self._resume_callback)
        self.create_service(Trigger, "stop", self._stop_callback)
        self.create_service(Trigger, "disable", self._disable_callback)

        period_s = 1.0 / self.control_rate_hz
        self._adapter = PiperSdkAdapter(
            can_name=self.can_name,
            control_period_s=period_s,
        )
        self._motion_enabled = False
        self._hardware_stop_required = False
        self._latest_command: tuple[float, ...] | None = None
        self._last_command_monotonic: float | None = None
        self._enabled_monotonic: float | None = None
        self._last_feedback: PiperFeedback | None = None
        self._last_sdk_timestamp_s: float | None = None
        self._last_feedback_change_monotonic: float | None = None
        self._fault_reason = "startup"

        try:
            self._adapter.connect()
            initial_feedback = self._adapter.wait_for_feedback(
                startup_feedback_timeout_s
            )
            (
                self._hardware_joint_limits_rad,
                self._hardware_max_joint_speed_rad_s,
            ) = self._adapter.read_joint_limits()
        except Exception:
            self._adapter.disconnect()
            raise
        now = time.monotonic()
        self._last_feedback = initial_feedback
        self._last_sdk_timestamp_s = initial_feedback.sdk_timestamp_s
        self._last_feedback_change_monotonic = now
        self._fault_reason = "" if initial_feedback.healthy else "unhealthy_startup_feedback"

        self.create_timer(period_s, self._control_tick)
        self.create_timer(1.0 / self.diagnostic_rate_hz, self._publish_diagnostics)
        self.get_logger().info(
            f"PiPER bridge connected on {self.can_name}; command gate starts closed, "
            f"control_rate={self.control_rate_hz:.1f}Hz"
        )

    def shutdown_hardware(self) -> None:
        self._close_command_gate()
        try:
            if self._hardware_stop_required:
                stop_error = self._quick_stop_hardware()
                if stop_error is not None:
                    self.get_logger().error(
                        f"shutdown quick stop failed: {stop_error}"
                    )
        finally:
            self._adapter.disconnect()

    def _command_callback(self, message: JointTrajectory) -> None:
        if not self._motion_enabled:
            return
        try:
            if len(message.points) != 1:
                raise CommandValidationError(
                    "joint_command must contain exactly one trajectory point"
                )
            command = normalize_joint_command(
                message.joint_names,
                message.points[0].positions,
                self._hardware_joint_limits_rad,
            )
        except CommandValidationError as exc:
            self.get_logger().error(f"rejecting joint command: {exc}")
            self._trip(f"invalid_command: {exc}")
            return
        self._latest_command = command
        self._last_command_monotonic = time.monotonic()

    def _control_tick(self) -> None:
        now = time.monotonic()
        try:
            feedback = self._adapter.read_feedback(require_speed_samples=True)
        except Exception as exc:
            self._fault_reason = f"feedback_error: {exc}"
            if self._motion_enabled or self._hardware_stop_required:
                self._trip(self._fault_reason)
            return

        self._last_feedback = feedback
        if feedback.sdk_timestamp_s != self._last_sdk_timestamp_s:
            self._last_sdk_timestamp_s = feedback.sdk_timestamp_s
            self._last_feedback_change_monotonic = now
        self._publish_joint_state(feedback)

        feedback_age = self._feedback_age(now)
        if feedback_age > self.feedback_timeout_s:
            self._fault_reason = "joint_feedback_timeout"
            if self._motion_enabled or self._hardware_stop_required:
                self._trip(self._fault_reason)
            return
        if feedback.joint_hz <= 0.0 or feedback.status_hz <= 0.0:
            self._fault_reason = "feedback_rate_zero"
            if self._motion_enabled or self._hardware_stop_required:
                self._trip(self._fault_reason)
            return
        if feedback.arm_status != 0:
            if (
                feedback.arm_status == 1
                and not self._motion_enabled
                and not self._hardware_stop_required
            ):
                if not self._fault_reason or self._fault_reason == "unhealthy_startup_feedback":
                    self._fault_reason = "quick_stop_latched"
                return
            self._fault_reason = f"arm_status_{feedback.arm_status}"
            if self._motion_enabled or self._hardware_stop_required:
                self._trip(self._fault_reason)
            return

        if not self._motion_enabled:
            if self._fault_reason.startswith(
                (
                    "feedback_error",
                    "feedback_rate_zero",
                    "arm_status_",
                    "joint_feedback_timeout",
                    "unhealthy_startup_feedback",
                )
            ):
                self._fault_reason = ""
            return

        command_reference = self._last_command_monotonic
        if command_reference is None:
            command_reference = self._enabled_monotonic
        if command_reference is None or now - command_reference > self.command_timeout_s:
            self._trip("command_timeout")
            return
        if self._latest_command is not None:
            try:
                self._adapter.command_joint_positions(self._latest_command)
            except Exception as exc:
                self._trip(f"command_error: {exc}")

    def _publish_joint_state(self, feedback: PiperFeedback) -> None:
        message = JointState()
        message.header.stamp = self.get_clock().now().to_msg()
        message.name = list(JOINT_NAMES)
        message.position = list(feedback.positions_rad)
        message.velocity = list(feedback.velocities_rad_s)
        message.effort = list(feedback.efforts_nm)
        self._state_publisher.publish(message)

    def _enable_callback(
        self, _request: Trigger.Request, response: Trigger.Response
    ) -> Trigger.Response:
        if self._motion_enabled:
            response.success = True
            response.message = "PiPER command gate is already open"
            return response
        now = time.monotonic()
        feedback = self._last_feedback
        if feedback is None or self._feedback_age(now) > self.feedback_timeout_s:
            response.success = False
            response.message = "fresh PiPER feedback is required before enable"
            return response
        if not feedback.healthy:
            response.success = False
            response.message = (
                "PiPER feedback is unhealthy: "
                f"joint_hz={feedback.joint_hz:.3f}, "
                f"status_hz={feedback.status_hz:.3f}, "
                f"arm_status={feedback.arm_status}"
            )
            return response

        self._hardware_stop_required = True
        try:
            enabled = self._adapter.enable(self.enable_timeout_s)
            if not enabled:
                raise PiperSdkError(
                    "PiPER did not report all motors enabled before timeout"
                )
            feedback = self._adapter.prepare_joint_control(self.enable_timeout_s)
        except Exception as exc:
            self._close_command_gate()
            stop_error = self._quick_stop_hardware()
            response.success = False
            response.message = f"enable failed: {exc}"
            if stop_error is not None:
                response.message += f"; quick_stop_failed: {stop_error}"
            self._fault_reason = response.message
            return response

        now = time.monotonic()
        self._last_feedback = feedback
        self._last_sdk_timestamp_s = feedback.sdk_timestamp_s
        self._last_feedback_change_monotonic = now
        self._motion_enabled = True
        self._latest_command = None
        self._last_command_monotonic = None
        self._enabled_monotonic = time.monotonic()
        self._fault_reason = ""
        response.success = True
        response.message = (
            "motor enable and CAN/MIT-high-follow mode confirmed; command gate "
            "opened; send a fresh "
            f"joint_command within {self.command_timeout_s:.3f}s"
        )
        return response

    def _resume_callback(
        self, _request: Trigger.Request, response: Trigger.Response
    ) -> Trigger.Response:
        if self._motion_enabled:
            response.success = False
            response.message = "close the command gate before requesting resume"
            return response
        try:
            feedback = self._adapter.read_feedback(require_speed_samples=False)
            if feedback.joint_hz <= 0.0 or feedback.status_hz <= 0.0:
                raise PiperSdkError("fresh PiPER feedback is required before resume")
            if feedback.arm_status not in (0, 1):
                response.success = False
                response.message = (
                    "software resume is only allowed for normal/quick-stop status; "
                    f"arm_status={feedback.arm_status}"
                )
                return response
            self._hardware_stop_required = True
            feedback = self._adapter.resume(self.resume_timeout_s)
        except Exception as exc:
            self._close_command_gate()
            stop_error = self._quick_stop_hardware()
            self._fault_reason = f"resume_failed: {exc}"
            if stop_error is not None:
                self._fault_reason += f"; quick_stop_failed: {stop_error}"
            response.success = False
            response.message = self._fault_reason
            return response

        now = time.monotonic()
        self._last_feedback = feedback
        self._last_sdk_timestamp_s = feedback.sdk_timestamp_s
        self._last_feedback_change_monotonic = now
        self._close_command_gate()
        self._fault_reason = ""
        response.success = True
        response.message = (
            "quick-stop state cleared; bridge command gate remains closed"
        )
        return response

    def _stop_callback(
        self, _request: Trigger.Request, response: Trigger.Response
    ) -> Trigger.Response:
        self._close_command_gate()
        stop_error = self._quick_stop_hardware()
        if stop_error is None:
            self._fault_reason = "manual_stop"
            response.success = True
            response.message = "PiPER quick stop sent"
        else:
            self._fault_reason = f"manual_stop_failed: {stop_error}"
            response.success = False
            response.message = self._fault_reason
        return response

    def _disable_callback(
        self, _request: Trigger.Request, response: Trigger.Response
    ) -> Trigger.Response:
        self._close_command_gate()
        self._hardware_stop_required = True
        try:
            disabled = self._adapter.disable(self.disable_timeout_s)
            if not disabled:
                raise PiperSdkError("PiPER did not report disabled before timeout")
        except Exception as exc:
            stop_error = self._quick_stop_hardware()
            self._fault_reason = f"manual_disable_failed: {exc}"
            if stop_error is not None:
                self._fault_reason += f"; quick_stop_failed: {stop_error}"
            response.success = False
            response.message = self._fault_reason
            return response

        self._hardware_stop_required = False
        self._fault_reason = "manual_disable"
        response.success = True
        response.message = "PiPER motor disable confirmed"
        return response

    def _trip(self, reason: str) -> None:
        if not self._motion_enabled and not self._hardware_stop_required:
            self._fault_reason = reason
            return
        self.get_logger().error(f"motion stopped: {reason}")
        self._close_command_gate()
        stop_error = self._quick_stop_hardware()
        self._fault_reason = reason
        if stop_error is not None:
            self.get_logger().error(stop_error)
            self._fault_reason += f"; quick_stop_failed: {stop_error}"

    def _close_command_gate(self) -> None:
        self._motion_enabled = False
        self._latest_command = None
        self._last_command_monotonic = None
        self._enabled_monotonic = None

    def _quick_stop_hardware(self) -> str | None:
        self._hardware_stop_required = True
        try:
            self._adapter.quick_stop()
        except Exception as exc:
            return str(exc)
        self._hardware_stop_required = False
        return None

    def _feedback_age(self, now: float | None = None) -> float:
        if self._last_feedback_change_monotonic is None:
            return math.inf
        if now is None:
            now = time.monotonic()
        return now - self._last_feedback_change_monotonic

    def _publish_diagnostics(self) -> None:
        now = time.monotonic()
        feedback = self._last_feedback
        if feedback is None:
            level = DiagnosticStatus.ERROR
            message_text = "no feedback"
        elif self._fault_reason in (
            "manual_stop", "manual_disable", "quick_stop_latched"
        ):
            level = DiagnosticStatus.WARN
            message_text = self._fault_reason
        elif self._fault_reason:
            level = DiagnosticStatus.ERROR
            message_text = self._fault_reason
        elif not self._motion_enabled:
            level = DiagnosticStatus.WARN
            message_text = "connected; command gate closed"
        else:
            level = DiagnosticStatus.OK
            message_text = "command gate open"

        command_age = math.inf
        if self._last_command_monotonic is not None:
            command_age = now - self._last_command_monotonic
        values = {
            "can_name": self.can_name,
            "command_gate_open": str(self._motion_enabled).lower(),
            "hardware_stop_required": str(self._hardware_stop_required).lower(),
            "joint_control_mode": "move_j_mit_high_follow",
            "motion_ctrl_2": "0x01,0x01,0,0xAD",
            "hardware_joint_min_deg": ",".join(
                f"{math.degrees(lower):.1f}"
                for lower, _upper in self._hardware_joint_limits_rad
            ),
            "hardware_joint_max_deg": ",".join(
                f"{math.degrees(upper):.1f}"
                for _lower, upper in self._hardware_joint_limits_rad
            ),
            "hardware_max_joint_speed_rad_s": ",".join(
                f"{speed:.3f}" for speed in self._hardware_max_joint_speed_rad_s
            ),
            "feedback_age_s": self._format_age(self._feedback_age(now)),
            "command_age_s": self._format_age(command_age),
            "arm_status": str(feedback.arm_status if feedback else -1),
            "ctrl_mode": str(feedback.ctrl_mode if feedback else -1),
            "joint_hz": f"{feedback.joint_hz:.3f}" if feedback else "0.000",
            "status_hz": f"{feedback.status_hz:.3f}" if feedback else "0.000",
            "speed_sample_count": (
                ",".join(str(value) for value in feedback.speed_sample_count)
                if feedback
                else ""
            ),
        }
        status = DiagnosticStatus()
        status.level = level
        status.name = "piper_bridge/arm"
        status.message = message_text
        status.hardware_id = f"piper:{self.can_name}"
        status.values = [KeyValue(key=key, value=value) for key, value in values.items()]
        array = DiagnosticArray()
        array.header.stamp = self.get_clock().now().to_msg()
        array.status = [status]
        self._diagnostics_publisher.publish(array)

    @staticmethod
    def _format_age(value: float) -> str:
        return "inf" if not math.isfinite(value) else f"{value:.6f}"


def main(args: list[str] | None = None) -> None:
    rclpy.init(args=args)
    node: PiperBridgeNode | None = None
    try:
        node = PiperBridgeNode()
        rclpy.spin(node)
    finally:
        if node is not None:
            node.shutdown_hardware()
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
