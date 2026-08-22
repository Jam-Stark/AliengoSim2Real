from __future__ import annotations

import argparse
import sys
from pathlib import Path

import rclpy
from rclpy.utilities import remove_ros_args

from .client import PiperBridgeClient
from .krushell_facade import PiperSdkRos2Facade


def _build_parser(default_target: tuple[float, float, float]) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run krushell/piper_sdk deployment/manipulation.py unchanged while "
            "routing its tested PiPER SDK calls through the A2 PC2 ROS 2 bridge."
        )
    )
    parser.add_argument("--namespace", default="/piper")
    parser.add_argument("--checkpoint_path", type=Path, default=None)
    parser.add_argument("--device", default="cpu")
    target_group = parser.add_mutually_exclusive_group()
    target_group.add_argument(
        "--target_pos_b",
        type=float,
        nargs=3,
        metavar=("X", "Y", "Z"),
        default=None,
    )
    target_group.add_argument("--random_target", action="store_true")
    parser.add_argument("--run_policy", action="store_true")
    parser.add_argument(
        "--resume_before_enable",
        action="store_true",
        help="explicitly clear a previous bridge quick stop before task reset/enable",
    )
    parser.add_argument(
        "--policy_steps",
        type=int,
        default=0,
        help="0 runs until interrupted",
    )
    parser.set_defaults(default_target=default_target)
    return parser


def main(args: list[str] | None = None) -> None:
    raw_args = list(sys.argv if args is None else [sys.argv[0], *args])
    rclpy.init(args=raw_args)
    client: PiperBridgeClient | None = None
    facade: PiperSdkRos2Facade | None = None
    task = None
    try:
        try:
            import piper_sdk.deployment.manipulation as manipulation_module
        except ImportError as exc:
            raise RuntimeError(
                "install krushell/piper_sdk in the laptop policy environment"
            ) from exc

        parser = _build_parser(tuple(manipulation_module.DEFAULT_TARGET_POS_B))
        cli_args = remove_ros_args(raw_args)[1:]
        if cli_args[:1] == ["--"]:
            cli_args = cli_args[1:]
        parsed = parser.parse_args(cli_args)
        if parsed.run_policy and parsed.checkpoint_path is None:
            parser.error("--run_policy requires --checkpoint_path")
        if parsed.policy_steps < 0:
            parser.error("--policy_steps must be non-negative")

        if parsed.random_target:
            target_pos_b = None
        elif parsed.target_pos_b is not None:
            target_pos_b = parsed.target_pos_b
        else:
            target_pos_b = parsed.default_target

        client = PiperBridgeClient(namespace=parsed.namespace)
        if parsed.resume_before_enable:
            success, message = client.resume()
            if not success:
                raise RuntimeError(message)
            print(message)
        facade = PiperSdkRos2Facade(client)
        original_factory = manipulation_module.C_PiperInterface_V2
        manipulation_module.C_PiperInterface_V2 = lambda _can_name: facade
        try:
            task = manipulation_module.Manipulation(
                checkpoint_path=parsed.checkpoint_path,
                device=parsed.device,
                can_name="remote_ros2",
                target_pos_b=target_pos_b,
            )
        finally:
            manipulation_module.C_PiperInterface_V2 = original_factory

        if not facade.connected or not facade.enabled:
            raise RuntimeError(
                "remote manipulation initialization did not complete PiPER enable/reset"
            )
        if parsed.run_policy:
            try:
                task.run_policy(
                    num_steps=parsed.policy_steps if parsed.policy_steps > 0 else None
                )
            finally:
                task.print_target_status()
    except KeyboardInterrupt:
        print("policy control interrupted")
    finally:
        if task is not None and facade is not None and facade.enabled:
            try:
                task.quick_stop()
            except Exception as exc:
                print(f"warning: remote quick stop failed: {exc}", file=sys.stderr)
        elif facade is not None and facade.enabled:
            try:
                facade.MotionCtrl_1(0x01, 0, 0)
            except Exception as exc:
                print(f"warning: remote quick stop failed: {exc}", file=sys.stderr)
        if facade is not None:
            facade.DisconnectPort()
        if client is not None:
            client.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
