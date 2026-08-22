from __future__ import annotations

import argparse
import math
import sys
import time

import rclpy
from rclpy.utilities import remove_ros_args

from .client import PiperBridgeClient


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PiPER PC2 bridge smoke test")
    parser.add_argument("--namespace", default="/piper")
    parser.add_argument(
        "--target-rad",
        type=float,
        nargs=6,
        default=(0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        metavar=("J1", "J2", "J3", "J4", "J5", "J6"),
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
    return parser


def main(args: list[str] | None = None) -> None:
    raw_args = list(sys.argv if args is None else [sys.argv[0], *args])
    rclpy.init(args=raw_args)
    client: PiperBridgeClient | None = None
    motion_started = False
    try:
        cli_args = remove_ros_args(raw_args)[1:]
        if cli_args[:1] == ["--"]:
            cli_args = cli_args[1:]
        parsed = _build_parser().parse_args(cli_args)
        client = PiperBridgeClient(parsed.namespace)
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
            if parsed.resume_before_enable:
                raise ValueError("--resume-before-enable requires --move")
            print("read-only smoke passed; no command was sent")
            return

        target = tuple(float(value) for value in parsed.target_rad)
        if parsed.resume_before_enable:
            success, message = client.resume()
            if not success:
                raise RuntimeError(message)
            print(message)
        success, message = client.enable()
        if not success:
            raise RuntimeError(message)
        motion_started = True
        deadline = time.monotonic() + parsed.timeout_s
        period_s = 0.02
        while time.monotonic() < deadline:
            cycle_start = time.monotonic()
            client.publish_joint_positions(target)
            client.pump(0.01)
            state = client.latest_state
            if state is None or not client.state_is_fresh(0.5):
                raise RuntimeError("joint state feedback is stale")
            max_error_deg = max(
                abs(math.degrees(actual - desired))
                for actual, desired in zip(state.positions_rad, target)
            )
            print(f"\rmax joint error={max_error_deg:.3f} deg", end="", flush=True)
            if max_error_deg <= parsed.tolerance_deg:
                print("\nmove smoke passed")
                return
            remaining = period_s - (time.monotonic() - cycle_start)
            if remaining > 0.0:
                time.sleep(remaining)
        raise TimeoutError(f"target not reached within {parsed.timeout_s:.1f}s")
    finally:
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
