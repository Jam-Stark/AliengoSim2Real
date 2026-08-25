from __future__ import annotations

import argparse
import math
import sys
import time

import rclpy
from rclpy.utilities import remove_ros_args
from rclpy.qos import qos_profile_sensor_data

from .client import PiperBridgeClient
from .model import JOINT_NAMES, normalize_joint_command


class RemoteImmediateStop(RuntimeError):
    pass


class RemoteControlledReturn(RuntimeError):
    pass


class A2RemoteStopMonitor:
    def __init__(self) -> None:
        self.received_monotonic_s: float | None = None
        self.immediate_reason: str | None = None
        self.controlled_return_requested = False

    def callback(self, message) -> None:
        packet = bytes(message.wireless_remote)
        if len(packet) != 40:
            return
        byte2 = packet[2]
        byte3 = packet[3]
        select_pressed = bool(byte2 & (1 << 3))
        l2_pressed = bool(byte2 & (1 << 5))
        b_pressed = bool(byte3 & (1 << 1))
        self.received_monotonic_s = time.monotonic()
        if select_pressed:
            self.immediate_reason = "Select"
        elif (
            b_pressed
            and not l2_pressed
            and not self.controlled_return_requested
        ):
            self.immediate_reason = "B handover cancel"
        if l2_pressed and b_pressed:
            self.controlled_return_requested = True

    def check(self, allow_controlled_return: bool = True) -> None:
        if self.immediate_reason is not None:
            raise RemoteImmediateStop(self.immediate_reason)
        if allow_controlled_return and self.controlled_return_requested:
            raise RemoteControlledReturn("L2+B")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PiPER PC2 bridge smoke test")
    parser.add_argument("--namespace", default="/piper")
    target_group = parser.add_mutually_exclusive_group()
    target_group.add_argument(
        "--target-rad",
        type=float,
        nargs=6,
        default=None,
        metavar=("J1", "J2", "J3", "J4", "J5", "J6"),
    )
    target_group.add_argument(
        "--hold-current",
        action="store_true",
        help="command the measured startup joint position for a no-travel stop-path test",
    )
    target_group.add_argument(
        "--round-trip-target-rad",
        type=float,
        nargs=6,
        default=None,
        metavar=("J1", "J2", "J3", "J4", "J5", "J6"),
        help="smoothly reach this joint target, hold it, then return to startup position",
    )
    parser.add_argument(
        "--move",
        action="store_true",
        help="enable motion; without this flag the test is read-only",
    )
    parser.add_argument(
        "--resume-before-enable",
        action="store_true",
        help="explicitly clear a previous bridge quick stop before enabling",
    )
    parser.add_argument("--timeout-s", type=float, default=20.0)
    parser.add_argument("--tolerance-deg", type=float, default=0.5)
    parser.add_argument("--return-tolerance-deg", type=float, default=3.5)
    parser.add_argument("--transition-s", type=float, default=5.0)
    parser.add_argument("--hold-s", type=float, default=5.0)
    parser.add_argument(
        "--a2-remote-stop",
        action="store_true",
        help="arm Select/B quick stop and L2+B controlled return from /lowstate",
    )
    return parser


def _max_error_deg(actual, desired) -> float:
    return max(
        abs(math.degrees(actual_value - desired_value))
        for actual_value, desired_value in zip(actual, desired)
    )


def _joint_errors_deg(actual, desired) -> tuple[float, ...]:
    return tuple(
        math.degrees(actual_value - desired_value)
        for actual_value, desired_value in zip(actual, desired)
    )


def _publish_for_duration(
    client: PiperBridgeClient,
    start: tuple[float, ...],
    target: tuple[float, ...],
    duration_s: float,
    remote: A2RemoteStopMonitor | None = None,
    allow_controlled_return: bool = True,
) -> None:
    start_time = time.monotonic()
    period_s = 0.02
    while True:
        if remote is not None:
            remote.check(allow_controlled_return)
        cycle_start = time.monotonic()
        elapsed = cycle_start - start_time
        progress = min(elapsed / duration_s, 1.0)
        alpha = progress * progress * (3.0 - 2.0 * progress)
        command = tuple(
            start_value + alpha * (target_value - start_value)
            for start_value, target_value in zip(start, target)
        )
        client.publish_joint_positions(command)
        client.pump(0.01)
        client.require_command_gate_open()
        if client.latest_state is None or not client.state_is_fresh(0.5):
            raise RuntimeError("joint state feedback is stale")
        if progress >= 1.0:
            return
        remaining = period_s - (time.monotonic() - cycle_start)
        if remaining > 0.0:
            time.sleep(remaining)


def _hold_target(
    client: PiperBridgeClient,
    target: tuple[float, ...],
    duration_s: float,
    remote: A2RemoteStopMonitor | None = None,
) -> None:
    deadline = time.monotonic() + duration_s
    period_s = 0.02
    while time.monotonic() < deadline:
        if remote is not None:
            remote.check()
        cycle_start = time.monotonic()
        client.publish_joint_positions(target)
        client.pump(0.01)
        client.require_command_gate_open()
        if client.latest_state is None or not client.state_is_fresh(0.5):
            raise RuntimeError("joint state feedback is stale")
        remaining = period_s - (time.monotonic() - cycle_start)
        if remaining > 0.0:
            time.sleep(remaining)


def _wait_until_target(
    client: PiperBridgeClient,
    target: tuple[float, ...],
    tolerance_deg: float,
    timeout_s: float,
    remote: A2RemoteStopMonitor | None = None,
    allow_controlled_return: bool = True,
) -> float:
    deadline = time.monotonic() + timeout_s
    next_report = time.monotonic()
    period_s = 0.02
    while time.monotonic() < deadline:
        if remote is not None:
            remote.check(allow_controlled_return)
        cycle_start = time.monotonic()
        client.publish_joint_positions(target)
        client.pump(0.01)
        client.require_command_gate_open()
        state = client.latest_state
        if state is None or not client.state_is_fresh(0.5):
            raise RuntimeError("joint state feedback is stale")
        error_deg = _max_error_deg(state.positions_rad, target)
        now = time.monotonic()
        if now >= next_report:
            errors = _joint_errors_deg(state.positions_rad, target)
            print(
                "settling: state_rad="
                f"{[round(value, 5) for value in state.positions_rad]} "
                "error_deg="
                f"{[round(value, 3) for value in errors]} "
                f"max={error_deg:.3f}",
                flush=True,
            )
            next_report = now + 1.0
        if error_deg <= tolerance_deg:
            return error_deg
        remaining = period_s - (time.monotonic() - cycle_start)
        if remaining > 0.0:
            time.sleep(remaining)
    state = client.latest_state
    if state is None:
        raise TimeoutError(f"target not reached within {timeout_s:.1f}s")
    errors = _joint_errors_deg(state.positions_rad, target)
    raise TimeoutError(
        f"target not reached within {timeout_s:.1f}s; "
        f"state_rad={[round(value, 5) for value in state.positions_rad]}; "
        f"error_deg={[round(value, 3) for value in errors]}"
    )


def main(args: list[str] | None = None) -> None:
    raw_args = list(sys.argv if args is None else [sys.argv[0], *args])
    rclpy.init(args=raw_args)
    client: PiperBridgeClient | None = None
    remote: A2RemoteStopMonitor | None = None
    remote_subscription = None
    motion_started = False
    try:
        cli_args = remove_ros_args(raw_args)[1:]
        if cli_args[:1] == ["--"]:
            cli_args = cli_args[1:]
        parsed = _build_parser().parse_args(cli_args)
        client = PiperBridgeClient(parsed.namespace)
        if parsed.a2_remote_stop:
            try:
                from unitree_hg.msg import LowState
            except ImportError as exc:
                raise RuntimeError(
                    "--a2-remote-stop requires unitree_hg/msg/LowState"
                ) from exc
            remote = A2RemoteStopMonitor()
            remote_subscription = client.create_subscription(
                LowState, "/lowstate", remote.callback, qos_profile_sensor_data
            )
            deadline = time.monotonic() + 3.0
            while remote.received_monotonic_s is None and time.monotonic() < deadline:
                client.pump(0.05)
            if remote.received_monotonic_s is None:
                raise TimeoutError("no /lowstate remote packet within 3.0s")
            remote.check()
            print("A2 remote armed: Select/B=quick stop, L2+B=controlled return")
        state = client.wait_for_state(3.0)
        diagnostics = client.wait_for_diagnostics(3.0)
        print(
            "state(rad)=",
            [round(value, 5) for value in state.positions_rad],
            "velocity(rad/s)=",
            [round(value, 5) for value in state.velocities_rad_s],
        )
        print("bridge=", diagnostics.message, diagnostics.values)
        if not parsed.move:
            if (
                parsed.resume_before_enable
                or parsed.hold_current
                or parsed.round_trip_target_rad is not None
            ):
                raise ValueError(
                    "motion options require --move"
                )
            print("read-only smoke passed; no command was sent")
            return

        if parsed.transition_s <= 0.0 or parsed.hold_s <= 0.0:
            raise ValueError("--transition-s and --hold-s must be positive")

        if parsed.round_trip_target_rad is not None:
            target = tuple(float(value) for value in parsed.round_trip_target_rad)
        elif parsed.hold_current:
            target = state.positions_rad
        elif parsed.target_rad is not None:
            target = tuple(float(value) for value in parsed.target_rad)
        else:
            target = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        normalize_joint_command(JOINT_NAMES, target)
        if parsed.resume_before_enable:
            success, message = client.resume()
            if not success:
                raise RuntimeError(message)
            print(message)
        success, message = client.enable()
        if not success:
            raise RuntimeError(message)
        client.publish_joint_positions(tuple(state.positions_rad))
        diagnostics = client.wait_for_command_gate_open(3.0)
        print(diagnostics.message, diagnostics.values)
        motion_started = True
        if parsed.round_trip_target_rad is not None:
            startup = tuple(state.positions_rad)
            print(
                f"round trip: transition={parsed.transition_s:.1f}s "
                f"hold={parsed.hold_s:.1f}s"
            )
            try:
                _publish_for_duration(
                    client, startup, target, parsed.transition_s, remote
                )
                target_error_deg = _wait_until_target(
                    client,
                    target,
                    parsed.tolerance_deg,
                    parsed.timeout_s,
                    remote,
                )
                print(f"target max joint error={target_error_deg:.3f} deg")
                _hold_target(client, target, parsed.hold_s, remote)
                _publish_for_duration(
                    client, target, startup, parsed.transition_s, remote
                )
                return_error_deg = _wait_until_target(
                    client,
                    startup,
                    parsed.return_tolerance_deg,
                    parsed.timeout_s,
                    remote,
                )
            except (RemoteControlledReturn, TimeoutError) as exc:
                current = client.latest_state
                if current is None:
                    raise RuntimeError("no joint state for controlled return")
                print(f"{exc}: returning to startup pose before stop")
                _publish_for_duration(
                    client,
                    current.positions_rad,
                    startup,
                    parsed.transition_s,
                    remote,
                    False,
                )
                client.pump(0.05)
                returned = client.latest_state
                if returned is None:
                    raise RuntimeError("no feedback after controlled return")
                return_errors = _joint_errors_deg(
                    returned.positions_rad, startup
                )
                print(
                    "controlled return state_rad="
                    f"{[round(value, 5) for value in returned.positions_rad]} "
                    "error_deg="
                    f"{[round(value, 3) for value in return_errors]}",
                    flush=True,
                )
                if isinstance(exc, RemoteControlledReturn):
                    raise RuntimeError(
                        "baseline cancelled by L2+B after controlled return"
                    )
                raise
            print(f"return max joint error={return_error_deg:.3f} deg")
            print("round-trip move smoke passed")
            return

        deadline = time.monotonic() + parsed.timeout_s
        period_s = 0.02
        while time.monotonic() < deadline:
            cycle_start = time.monotonic()
            client.publish_joint_positions(target)
            client.pump(0.01)
            client.require_command_gate_open()
            state = client.latest_state
            if state is None or not client.state_is_fresh(0.5):
                raise RuntimeError("joint state feedback is stale")
            max_error_deg = _max_error_deg(state.positions_rad, target)
            print(f"\rmax joint error={max_error_deg:.3f} deg", end="", flush=True)
            if max_error_deg <= parsed.tolerance_deg:
                print("\nmove smoke passed")
                return
            remaining = period_s - (time.monotonic() - cycle_start)
            if remaining > 0.0:
                time.sleep(remaining)
        raise TimeoutError(f"target not reached within {parsed.timeout_s:.1f}s")
    except RemoteImmediateStop as exc:
        raise RuntimeError(f"baseline cancelled by A2 remote {exc}") from exc
    finally:
        del remote_subscription
        if client is not None and motion_started:
            success, message = client.stop()
            if not success:
                print(f"warning: stop service failed: {message}", file=sys.stderr)
        if client is not None:
            client.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
