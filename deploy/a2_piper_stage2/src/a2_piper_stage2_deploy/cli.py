"""Command line entrypoint for bundle and offline Stage2 verification."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

import numpy as np

from .contract import PolicyContract
from .observations import PolicyCommand, RobotSnapshot
from .policy import TorchScriptActors, run_bundle_validator, validate_contract_references
from .ros_transport import run_ros_live, run_ros_shadow
from .runtime import DualPolicyRuntime
from .site import SiteConfig


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate", help="run authoritative bundle parity")
    validate.add_argument("--bundle", type=Path, default=Path("policy_bundle"))
    validate.add_argument("--atol", type=float, default=1.0e-6)

    benchmark = subparsers.add_parser("benchmark", help="measure arm+dog CPU latency")
    benchmark.add_argument("--bundle", type=Path, default=Path("policy_bundle"))
    benchmark.add_argument("--warmup", type=int, default=200)
    benchmark.add_argument("--iterations", type=int, default=2000)

    mock = subparsers.add_parser("mock-shadow", help="run the real actors on deterministic mock state")
    mock.add_argument("--bundle", type=Path, default=Path("policy_bundle"))
    mock.add_argument("--ticks", type=int, default=500)
    mock.add_argument("--realtime", action="store_true")

    site_check = subparsers.add_parser("site-check", help="list unresolved site fields")
    site_check.add_argument("--site", type=Path, required=True)

    ros_shadow = subparsers.add_parser("ros-shadow", help="attach to verified read-only ROS state")
    ros_shadow.add_argument("--site", type=Path, required=True)

    ros_live = subparsers.add_parser("ros-live", help="run the coupled live ROS path")
    ros_live.add_argument("--site", type=Path, required=True)
    ros_live.add_argument("--live", action="store_true")
    return parser


def main() -> None:
    args = _parser().parse_args()
    if args.command == "validate":
        contract = _load_contract(args.bundle)
        print(
            json.dumps(
                {
                    "model_reference_parity": run_bundle_validator(args.bundle, args.atol),
                    "manifest_reference_parity": validate_contract_references(contract, args.atol),
                },
                indent=2,
            )
        )
    elif args.command == "benchmark":
        contract = _load_contract(args.bundle)
        actors = TorchScriptActors(contract)
        print(json.dumps(actors.benchmark(args.warmup, args.iterations), indent=2))
    elif args.command == "mock-shadow":
        print(json.dumps(_mock_shadow(args.bundle, args.ticks, args.realtime), indent=2))
    elif args.command == "site-check":
        site = SiteConfig.load(args.site)
        print(json.dumps({"unresolved_fields": site.unresolved_fields()}, indent=2))
    elif args.command == "ros-shadow":
        run_ros_shadow(SiteConfig.load(args.site))
    elif args.command == "ros-live":
        run_ros_live(SiteConfig.load(args.site), args.live)


def _load_contract(bundle: Path) -> PolicyContract:
    return PolicyContract.load(bundle.expanduser().resolve() / "policy_manifest.yaml")


def _mock_shadow(bundle: Path, ticks: int, realtime: bool) -> dict[str, object]:
    contract = _load_contract(bundle)
    runtime = DualPolicyRuntime(contract, TorchScriptActors(contract))
    command = PolicyCommand(
        locomotion_vx_vy_yaw=np.asarray([0.0, 0.0, 0.0], dtype=np.float32),
        arm_goal_radius_pitch_yaw=np.asarray([0.6, 0.0, 0.0], dtype=np.float32),
    )
    latencies = np.empty(ticks, dtype=np.float64)
    for index in range(ticks):
        tick_started = time.monotonic()
        snapshot = RobotSnapshot(
            monotonic_time_s=tick_started,
            root_quaternion_xyzw=np.asarray([0.0, 0.0, 0.0, 1.0], dtype=np.float32),
            dog_joint_position_rad=contract.dog.default_position_rad.copy(),
            dog_joint_velocity_rad_s=np.zeros(12, dtype=np.float32),
            arm_joint_position_rad=contract.arm.default_position_rad.copy(),
        )
        output = runtime.tick(snapshot, command)
        latencies[index] = output.inference_latency_ms
        if realtime:
            remaining = contract.policy_period_s - (time.monotonic() - tick_started)
            if remaining > 0.0:
                time.sleep(remaining)
    return {
        "status": "pass",
        "mode": "mock_shadow",
        "ticks": ticks,
        "hardware_output": False,
        "latency_ms": {
            "mean": float(np.mean(latencies)),
            "p95": float(np.percentile(latencies, 95)),
            "max": float(np.max(latencies)),
        },
    }


if __name__ == "__main__":
    main()
