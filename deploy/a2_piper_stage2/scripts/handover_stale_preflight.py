#!/usr/bin/env python3

import time

import rclpy
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
from rclpy.executors import MultiThreadedExecutor
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState
from std_msgs.msg import String
from std_srvs.srv import Trigger
from trajectory_msgs.msg import JointTrajectory
from unitree_hg.msg import LowCmd, LowState


class A2StateFixture(Node):
    def __init__(self, started_at: float) -> None:
        super().__init__("handover_stale_a2_fixture")
        self.started_at = started_at
        self.lowstate_pub = self.create_publisher(LowState, "/fixture/lowstate", 10)
        self.create_timer(0.02, self._publish_lowstate)

    def _publish_lowstate(self) -> None:
        elapsed = time.monotonic() - self.started_at
        lowstate = LowState()
        lowstate.imu_state.quaternion = [1.0, 0.0, 0.0, 0.0]
        training_default = [
            0.0,
            0.0,
            0.0,
            0.0,
            0.5,
            0.5,
            0.5,
            0.5,
            -1.0,
            -1.0,
            -1.0,
            -1.0,
        ]
        training_to_a2 = [3, 0, 9, 6, 4, 1, 10, 7, 5, 2, 11, 8]
        for training_index, a2_index in enumerate(training_to_a2):
            lowstate.motor_state[a2_index].q = training_default[training_index]
        if 1.0 <= elapsed < 1.12:
            lowstate.wireless_remote[3] = 1
        self.lowstate_pub.publish(lowstate)


class HandoverStaleFixture(Node):
    def __init__(self, started_at: float) -> None:
        super().__init__("handover_stale_piper_fixture")
        self.started_at = started_at
        self.started_at = time.monotonic()
        self.gate_open = False
        self.lowcmd_count = 0
        self.piper_command_count = 0
        self.saw_enable_hold = False
        self.saw_standup = False

        self.piper_state_pub = self.create_publisher(
            JointState, "/fixture/piper/joint_states", qos_profile_sensor_data
        )
        self.diagnostics_pub = self.create_publisher(
            DiagnosticArray, "/fixture/piper/diagnostics", 10
        )
        self.create_subscription(LowCmd, "/fixture/lowcmd", self._lowcmd, 10)
        self.create_subscription(
            JointTrajectory,
            "/fixture/piper/joint_command",
            self._piper_command,
            qos_profile_sensor_data,
        )
        self.create_subscription(String, "/fixture/status", self._status, 10)
        self.create_service(Trigger, "/fixture/piper/resume", self._resume)
        self.create_service(Trigger, "/fixture/piper/enable", self._enable)
        self.create_service(Trigger, "/fixture/piper/stop", self._stop)
        self.create_timer(0.02, self._publish_state)

    def _lowcmd(self, _message: LowCmd) -> None:
        self.lowcmd_count += 1

    def _piper_command(self, _message: JointTrajectory) -> None:
        self.piper_command_count += 1

    def _status(self, message: String) -> None:
        if "holding measured positions while PiPER enable stabilizes" in message.data:
            self.saw_enable_hold = True
        if "phase=StandUpInterpolating" in message.data:
            self.saw_standup = True

    def _resume(self, _request: Trigger.Request, response: Trigger.Response):
        response.success = True
        response.message = "fixture resume accepted"
        return response

    def _enable(self, _request: Trigger.Request, response: Trigger.Response):
        time.sleep(0.70)
        self.gate_open = True
        response.success = True
        response.message = "fixture stable enable accepted"
        return response

    def _stop(self, _request: Trigger.Request, response: Trigger.Response):
        self.gate_open = False
        response.success = True
        response.message = "fixture stop accepted"
        return response

    def _publish_state(self) -> None:
        joint_state = JointState()
        joint_state.header.stamp = self.get_clock().now().to_msg()
        joint_state.name = [f"arm_j{index}" for index in range(1, 7)]
        joint_state.position = [0.0, 1.48, -0.63, -0.84, 0.0, 1.57]
        joint_state.velocity = [0.0] * 6
        self.piper_state_pub.publish(joint_state)

        diagnostics = DiagnosticArray()
        diagnostics.header.stamp = self.get_clock().now().to_msg()
        status = DiagnosticStatus()
        status.level = DiagnosticStatus.OK if self.gate_open else DiagnosticStatus.WARN
        status.name = "fixture/piper"
        status.message = "command gate open" if self.gate_open else "gate closed"
        status.values = [
            KeyValue(
                key="command_gate_open", value=str(self.gate_open).lower()
            )
        ]
        diagnostics.status = [status]
        self.diagnostics_pub.publish(diagnostics)


def main() -> int:
    rclpy.init()
    started_at = time.monotonic()
    node = HandoverStaleFixture(started_at)
    a2_node = A2StateFixture(started_at)
    executor = MultiThreadedExecutor(num_threads=2)
    executor.add_node(node)
    executor.add_node(a2_node)
    deadline = time.monotonic() + 5.0
    while rclpy.ok() and time.monotonic() < deadline and not node.saw_standup:
        executor.spin_once(timeout_sec=0.05)
    print(f"saw_enable_hold={str(node.saw_enable_hold).lower()}")
    print(f"saw_standup={str(node.saw_standup).lower()}")
    print(f"isolated_lowcmd_count={node.lowcmd_count}")
    print(f"isolated_piper_command_count={node.piper_command_count}")
    passed = (
        node.saw_enable_hold
        and node.saw_standup
        and node.lowcmd_count > 0
        and node.piper_command_count > 0
    )
    executor.remove_node(a2_node)
    executor.remove_node(node)
    a2_node.destroy_node()
    node.destroy_node()
    rclpy.shutdown()
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
