from __future__ import annotations

import time
from dataclasses import dataclass

import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from rclpy.node import Node
from rclpy.qos import (
    DurabilityPolicy,
    HistoryPolicy,
    QoSProfile,
    ReliabilityPolicy,
    SensorDataQoS,
)
from sensor_msgs.msg import JointState
from std_srvs.srv import Trigger
from trajectory_msgs.msg import JointTrajectory, JointTrajectoryPoint

from .model import JOINT_NAMES, normalize_joint_command, order_joint_values


@dataclass(frozen=True)
class RemoteJointState:
    positions_rad: tuple[float, ...]
    velocities_rad_s: tuple[float, ...]
    efforts_nm: tuple[float, ...]
    source_timestamp_s: float
    received_wall_time_s: float
    received_monotonic_s: float
    measured_hz: float


@dataclass(frozen=True)
class RemoteDiagnostics:
    level: int
    message: str
    values: dict[str, str]
    received_monotonic_s: float


class PiperBridgeClient(Node):
    """Laptop-side client for the PC2 PiPER bridge."""

    def __init__(self, namespace: str = "/piper") -> None:
        super().__init__("piper_remote_client")
        self.namespace = "/" + namespace.strip("/")
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
        self._command_publisher = self.create_publisher(
            JointTrajectory, self._topic("joint_command"), command_qos
        )
        self.create_subscription(
            JointState,
            self._topic("joint_states"),
            self._joint_state_callback,
            SensorDataQoS(),
        )
        self.create_subscription(
            DiagnosticArray,
            self._topic("diagnostics"),
            self._diagnostics_callback,
            diagnostics_qos,
        )
        self._enable_client = self.create_client(Trigger, self._topic("enable"))
        self._resume_client = self.create_client(Trigger, self._topic("resume"))
        self._stop_client = self.create_client(Trigger, self._topic("stop"))
        self._disable_client = self.create_client(Trigger, self._topic("disable"))
        self.latest_state: RemoteJointState | None = None
        self.latest_diagnostics: RemoteDiagnostics | None = None
        self._previous_state_arrival: float | None = None
        self._state_rate_hz = 0.0

    def pump(self, timeout_s: float = 0.0) -> None:
        rclpy.spin_once(self, timeout_sec=timeout_s)

    def wait_for_state(self, timeout_s: float) -> RemoteJointState:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.pump(min(0.05, max(0.0, deadline - time.monotonic())))
            if self.latest_state is not None:
                return self.latest_state
        raise TimeoutError(
            f"no {self._topic('joint_states')} message within {timeout_s:.1f}s"
        )

    def wait_for_diagnostics(self, timeout_s: float) -> RemoteDiagnostics:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.pump(min(0.05, max(0.0, deadline - time.monotonic())))
            if self.latest_diagnostics is not None:
                return self.latest_diagnostics
        raise TimeoutError(
            f"no {self._topic('diagnostics')} message within {timeout_s:.1f}s"
        )

    def enable(self, timeout_s: float = 6.0) -> tuple[bool, str]:
        return self._call_trigger(self._enable_client, "enable", timeout_s)

    def resume(self, timeout_s: float = 4.0) -> tuple[bool, str]:
        return self._call_trigger(self._resume_client, "resume", timeout_s)

    def stop(self, timeout_s: float = 2.0) -> tuple[bool, str]:
        return self._call_trigger(self._stop_client, "stop", timeout_s)

    def disable(self, timeout_s: float = 2.0) -> tuple[bool, str]:
        return self._call_trigger(self._disable_client, "disable", timeout_s)

    def publish_joint_positions(self, positions_rad: tuple[float, ...]) -> None:
        ordered = normalize_joint_command(JOINT_NAMES, positions_rad)
        message = JointTrajectory()
        message.header.stamp = self.get_clock().now().to_msg()
        message.joint_names = list(JOINT_NAMES)
        point = JointTrajectoryPoint()
        point.positions = list(ordered)
        point.time_from_start.nanosec = 20_000_000
        message.points = [point]
        self._command_publisher.publish(message)

    def state_is_fresh(self, max_age_s: float) -> bool:
        state = self.latest_state
        return state is not None and time.monotonic() - state.received_monotonic_s <= max_age_s

    def diagnostics_are_fresh(self, max_age_s: float) -> bool:
        diagnostics = self.latest_diagnostics
        return (
            diagnostics is not None
            and time.monotonic() - diagnostics.received_monotonic_s <= max_age_s
        )

    def _call_trigger(self, client, name: str, timeout_s: float) -> tuple[bool, str]:
        if not client.wait_for_service(timeout_sec=timeout_s):
            return False, f"{self._topic(name)} service unavailable"
        future = client.call_async(Trigger.Request())
        deadline = time.monotonic() + timeout_s
        while rclpy.ok() and not future.done() and time.monotonic() < deadline:
            self.pump(min(0.05, max(0.0, deadline - time.monotonic())))
        if not future.done():
            return False, f"{self._topic(name)} service timed out"
        response = future.result()
        if response is None:
            return False, f"{self._topic(name)} service failed"
        return bool(response.success), str(response.message)

    def _joint_state_callback(self, message: JointState) -> None:
        try:
            positions = order_joint_values(message.name, message.position, "position")
        except ValueError as exc:
            self.get_logger().error(f"invalid bridge joint state: {exc}")
            return
        try:
            velocities = order_joint_values(message.name, message.velocity, "velocity")
            efforts = order_joint_values(message.name, message.effort, "effort")
        except ValueError as exc:
            self.get_logger().error(f"invalid bridge joint state: {exc}")
            return
        now_monotonic = time.monotonic()
        if self._previous_state_arrival is not None:
            interval = now_monotonic - self._previous_state_arrival
            if interval > 0.0:
                instantaneous_hz = 1.0 / interval
                if self._state_rate_hz == 0.0:
                    self._state_rate_hz = instantaneous_hz
                else:
                    self._state_rate_hz = 0.9 * self._state_rate_hz + 0.1 * instantaneous_hz
        else:
            self._state_rate_hz = 0.0
        self._previous_state_arrival = now_monotonic
        source_timestamp = (
            float(message.header.stamp.sec)
            + float(message.header.stamp.nanosec) * 1e-9
        )
        self.latest_state = RemoteJointState(
            positions_rad=positions,
            velocities_rad_s=velocities,
            efforts_nm=efforts,
            source_timestamp_s=source_timestamp,
            received_wall_time_s=time.time(),
            received_monotonic_s=now_monotonic,
            measured_hz=self._state_rate_hz,
        )

    def _diagnostics_callback(self, message: DiagnosticArray) -> None:
        if not message.status:
            return
        status = message.status[0]
        values = {item.key: item.value for item in status.values}
        self.latest_diagnostics = RemoteDiagnostics(
            level=int(status.level),
            message=str(status.message),
            values=values,
            received_monotonic_s=time.monotonic(),
        )

    def _topic(self, suffix: str) -> str:
        return f"{self.namespace}/{suffix}"
