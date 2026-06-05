#!/usr/bin/env python3
"""A2 real-robot ROS2 observers for connected validation.

This script intentionally uses only ROS2 generated messages and local Python
logic. It does not call the project C++ CRC/runtime code.
"""

from __future__ import annotations

import argparse
import csv
import math
import struct
import sys
import time
from typing import Dict, Iterable, List, Sequence, Tuple


JOINT_LABELS = (
    "FR_BODY",
    "FR_THIGH",
    "FR_CALF",
    "FL_BODY",
    "FL_THIGH",
    "FL_CALF",
    "RR_BODY",
    "RR_THIGH",
    "RR_CALF",
    "RL_BODY",
    "RL_THIGH",
    "RL_CALF",
)
BUTTON_BYTE2 = ("R1", "L1", "Start", "Select", "R2", "L2", "F1", "F3")
BUTTON_BYTE3 = ("A", "B", "X", "Y", "Up", "Right", "Down", "Left")
STICK_NAMES = ("lx", "rx", "ry", "ly")
DEFAULT_LOWSTATE_TOPIC = "/lowstate"
DEFAULT_LOWCMD_TOPIC = "/lowcmd"

rclpy = None
Node = object
LowCmd = object
LowState = object


def ensure_ros_imports() -> None:
    global rclpy, Node, LowCmd, LowState
    if rclpy is not None:
        return
    import rclpy as rclpy_module
    from rclpy.node import Node as NodeClass
    from unitree_hg.msg import LowCmd as LowCmdClass
    from unitree_hg.msg import LowState as LowStateClass

    rclpy = rclpy_module
    Node = NodeClass
    LowCmd = LowCmdClass
    LowState = LowStateClass


def is_finite(value: float) -> bool:
    return math.isfinite(float(value))


def finite_values(values: Iterable[float]) -> bool:
    return all(is_finite(value) for value in values)


def spin_for(node: Node, duration: float) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline and rclpy.ok():
        rclpy.spin_once(node, timeout_sec=0.1)


def add_lowstate_topic_arg(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--lowstate-topic", default=DEFAULT_LOWSTATE_TOPIC)


def add_lowcmd_topic_arg(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--lowcmd-topic", default=DEFAULT_LOWCMD_TOPIC)


def bytes_from_uint8_sequence(data: Sequence[int]) -> bytes:
    return bytes(int(value) & 0xFF for value in data)


def decode_remote(raw_data: Sequence[int]) -> Tuple[Dict[str, float], Dict[str, bool], bool]:
    data = bytes_from_uint8_sequence(raw_data)
    if len(data) < 24:
        raise ValueError(f"wireless_remote length is {len(data)}, expected at least 24")

    sticks = {
        "lx": struct.unpack_from("<f", data, 4)[0],
        "rx": struct.unpack_from("<f", data, 8)[0],
        "ry": struct.unpack_from("<f", data, 12)[0],
        "ly": struct.unpack_from("<f", data, 20)[0],
    }
    buttons = {}
    for bit, name in enumerate(BUTTON_BYTE2):
        buttons[name] = bool((data[2] >> bit) & 0x1)
    for bit, name in enumerate(BUTTON_BYTE3):
        buttons[name] = bool((data[3] >> bit) & 0x1)

    valid = all(math.isfinite(value) for value in sticks.values())
    return sticks, buttons, valid


def display_stick(value: float, deadzone: float) -> float:
    if not math.isfinite(value):
        return value
    if abs(value) < deadzone:
        return 0.0
    return max(-1.0, min(1.0, value))


def pressed_button_names(buttons: Dict[str, bool]) -> List[str]:
    return [name for name in BUTTON_BYTE2 + BUTTON_BYTE3 if buttons.get(name)]


def format_buttons(buttons: Dict[str, bool]) -> str:
    names = pressed_button_names(buttons)
    return ",".join(names) if names else "none"


def collect_lowstate_floats(msg: LowState) -> List[float]:
    values: List[float] = []
    values.extend(float(value) for value in msg.imu_state.quaternion)
    values.extend(float(value) for value in msg.imu_state.gyroscope)
    values.extend(float(value) for value in msg.imu_state.accelerometer)
    values.extend(float(value) for value in msg.imu_state.rpy)
    for motor in msg.motor_state:
        values.extend(
            [
                float(motor.q),
                float(motor.dq),
                float(motor.ddq),
                float(motor.tau_est),
                float(motor.vol),
            ]
        )
    return values


def make_joint_stats() -> List[Dict[str, float]]:
    return [
        {
            "start": float("nan"),
            "end": float("nan"),
            "min": float("inf"),
            "max": float("-inf"),
            "max_abs_dq": 0.0,
        }
        for _ in JOINT_LABELS
    ]


def print_joint_snapshot(
    sample_index: int,
    relative_time: float,
    tick: int,
    q_values: Sequence[float],
    dq_values: Sequence[float],
    starts: Sequence[float],
) -> None:
    print(
        "joint_sample "
        f"sample={sample_index} t={relative_time:.3f}s tick={tick} "
        "observe_only_no_lowcmd_publish=True"
    )
    for index, label in enumerate(JOINT_LABELS):
        delta = float(q_values[index]) - float(starts[index])
        print(
            f"  {index:02d} {label:<8s} "
            f"q={float(q_values[index]):+8.4f} "
            f"dq={float(dq_values[index]):+8.4f} "
            f"delta_from_start={delta:+8.4f}"
        )


def joint_csv_header() -> List[str]:
    header = ["sample", "time_s", "monotonic_s", "tick"]
    for label in JOINT_LABELS:
        header.extend([f"{label}_q", f"{label}_dq", f"{label}_delta_from_start"])
    return header


def joint_csv_row(
    sample_index: int,
    relative_time: float,
    monotonic_time: float,
    tick: int,
    q_values: Sequence[float],
    dq_values: Sequence[float],
    starts: Sequence[float],
) -> List[float]:
    row: List[float] = [
        float(sample_index),
        float(relative_time),
        float(monotonic_time),
        float(tick),
    ]
    for index in range(len(JOINT_LABELS)):
        q = float(q_values[index])
        dq = float(dq_values[index])
        row.extend([q, dq, q - float(starts[index])])
    return row


def crc32_core_words(words: Iterable[int]) -> int:
    crc = 0xFFFFFFFF
    polynomial = 0x04C11DB7
    for word in words:
        xbit = 1 << 31
        data = int(word) & 0xFFFFFFFF
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) & 0xFFFFFFFF) ^ polynomial
            else:
                crc = (crc << 1) & 0xFFFFFFFF
            if data & xbit:
                crc ^= polynomial
            xbit >>= 1
        crc &= 0xFFFFFFFF
    return crc


def pack_lowcmd_for_crc(msg: LowCmd) -> bytes:
    payload = bytearray()
    payload.extend(struct.pack("<BB2x", int(msg.mode_pr), int(msg.mode_machine)))
    for motor in msg.motor_cmd:
        payload.extend(
            struct.pack(
                "<B3xfffffI",
                int(motor.mode),
                float(motor.q),
                float(motor.dq),
                float(motor.tau),
                float(motor.kp),
                float(motor.kd),
                int(motor.reserve) & 0xFFFFFFFF,
            )
        )

    reserve = list(msg.reserve)
    reserve.extend([0, 0, 0, 0])
    payload.extend(struct.pack("<4I", *(int(value) & 0xFFFFFFFF for value in reserve[:4])))

    if len(payload) != 1000:
        raise RuntimeError(f"packed LowCmd CRC region is {len(payload)} bytes, expected 1000")
    return bytes(payload)


def compute_lowcmd_crc(msg: LowCmd) -> int:
    payload = pack_lowcmd_for_crc(msg)
    words = struct.unpack("<250I", payload)
    return crc32_core_words(words)


def lowcmd_is_zero(msg: LowCmd) -> bool:
    for motor in msg.motor_cmd:
        if int(motor.mode) != 0x00:
            return False
        if (
            float(motor.q) != 0.0
            or float(motor.dq) != 0.0
            or float(motor.tau) != 0.0
            or float(motor.kp) != 0.0
            or float(motor.kd) != 0.0
        ):
            return False
    return True


def run_lowstate(args: argparse.Namespace) -> int:
    ensure_ros_imports()
    rclpy.init()
    node = rclpy.create_node("a2_real_lowstate_observer")
    stats = {
        "count": 0,
        "nonfinite": 0,
        "last_time": None,
        "first_time": None,
        "max_gap": 0.0,
        "last_tick": None,
        "tick_deltas": [],
        "mode_pr": set(),
        "mode_machine": set(),
    }

    def callback(msg: LowState) -> None:
        now = time.monotonic()
        if stats["first_time"] is None:
            stats["first_time"] = now
        if stats["last_time"] is not None:
            stats["max_gap"] = max(stats["max_gap"], now - stats["last_time"])
        stats["last_time"] = now
        stats["count"] += 1
        stats["mode_pr"].add(int(msg.mode_pr))
        stats["mode_machine"].add(int(msg.mode_machine))

        tick = int(msg.tick) & 0xFFFFFFFF
        if stats["last_tick"] is not None:
            stats["tick_deltas"].append((tick - stats["last_tick"]) & 0xFFFFFFFF)
        stats["last_tick"] = tick

        if not finite_values(collect_lowstate_floats(msg)):
            stats["nonfinite"] += 1

    node.create_subscription(LowState, args.lowstate_topic, callback, 50)
    print(f"lowstate_topic={args.lowstate_topic}")
    spin_for(node, args.duration)
    node.destroy_node()
    rclpy.shutdown()

    count = int(stats["count"])
    elapsed = args.duration
    if count > 1 and stats["first_time"] is not None and stats["last_time"] is not None:
        elapsed = max(1e-6, float(stats["last_time"]) - float(stats["first_time"]))
        hz = (count - 1) / elapsed
    else:
        hz = count / max(1e-6, args.duration)
    max_gap_ms = float(stats["max_gap"]) * 1000.0
    tick_deltas = list(stats["tick_deltas"])

    print(f"lowstate_count={count}")
    print(f"lowstate_rate_hz={hz:.2f}")
    print(f"max_interarrival_gap_ms={max_gap_ms:.2f}")
    print(f"mode_pr_values={sorted(stats['mode_pr'])}")
    print(f"mode_machine_values={sorted(stats['mode_machine'])}")
    print(f"nonfinite_message_count={stats['nonfinite']}")
    if tick_deltas:
        tick_avg = sum(tick_deltas) / len(tick_deltas)
        print(
            "tick_delta_ms="
            f"min={min(tick_deltas)} avg={tick_avg:.2f} max={max(tick_deltas)}"
        )
    else:
        print("tick_delta_ms=unavailable")

    failures = []
    if count == 0:
        failures.append(f"no {args.lowstate_topic} messages")
    if hz < args.min_hz:
        failures.append(f"rate {hz:.2f} Hz below min {args.min_hz:.2f} Hz")
    if max_gap_ms > args.max_gap_ms:
        failures.append(f"max gap {max_gap_ms:.2f} ms above max {args.max_gap_ms:.2f} ms")
    if int(stats["nonfinite"]) > 0:
        failures.append(f"{stats['nonfinite']} messages contain NaN/Inf")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 2
    print("PASS: lowstate freshness/rate/finiteness checks passed")
    return 0


def run_joints(args: argparse.Namespace) -> int:
    if args.duration <= 0.0:
        print("FAIL: duration must be positive", file=sys.stderr)
        return 2
    if args.print_period <= 0.0:
        print("FAIL: --print-period must be positive", file=sys.stderr)
        return 2
    if args.min_delta < 0.0:
        print("FAIL: --min-delta must be non-negative", file=sys.stderr)
        return 2

    ensure_ros_imports()
    rclpy.init()
    node = rclpy.create_node("a2_real_joint_observer")
    joint_stats = make_joint_stats()
    stats = {
        "lowstate_count": 0,
        "valid_sample_count": 0,
        "nonfinite_message_count": 0,
        "short_motor_state_count": 0,
        "first_valid_time": None,
        "last_print_time": None,
        "motor_state_lengths": set(),
        "last_tick": None,
    }
    csv_file = None
    csv_writer = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="", encoding="utf-8")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(joint_csv_header())
        csv_file.flush()

    def callback(msg: LowState) -> None:
        stats["lowstate_count"] += 1
        motor_count = len(msg.motor_state)
        stats["motor_state_lengths"].add(motor_count)
        if motor_count < len(JOINT_LABELS):
            stats["short_motor_state_count"] += 1
            return

        q_values = []
        dq_values = []
        for index in range(len(JOINT_LABELS)):
            motor = msg.motor_state[index]
            q_values.append(float(motor.q))
            dq_values.append(float(motor.dq))

        if not finite_values(q_values) or not finite_values(dq_values):
            stats["nonfinite_message_count"] += 1
            return

        now = time.monotonic()
        if stats["first_valid_time"] is None:
            stats["first_valid_time"] = now
            for index, q in enumerate(q_values):
                joint_stats[index]["start"] = q
                joint_stats[index]["min"] = q
                joint_stats[index]["max"] = q

        stats["valid_sample_count"] += 1
        sample_index = int(stats["valid_sample_count"])
        starts = [joint_stats[index]["start"] for index in range(len(JOINT_LABELS))]
        relative_time = now - float(stats["first_valid_time"])
        tick = int(msg.tick) & 0xFFFFFFFF
        stats["last_tick"] = tick

        for index, q in enumerate(q_values):
            joint_stats[index]["end"] = q
            joint_stats[index]["min"] = min(joint_stats[index]["min"], q)
            joint_stats[index]["max"] = max(joint_stats[index]["max"], q)
            joint_stats[index]["max_abs_dq"] = max(
                joint_stats[index]["max_abs_dq"],
                abs(float(dq_values[index])),
            )

        last_print_time = stats["last_print_time"]
        if last_print_time is None or (now - float(last_print_time)) >= args.print_period:
            print_joint_snapshot(sample_index, relative_time, tick, q_values, dq_values, starts)
            stats["last_print_time"] = now

        if csv_writer is not None:
            csv_writer.writerow(
                joint_csv_row(
                    sample_index,
                    relative_time,
                    now,
                    tick,
                    q_values,
                    dq_values,
                    starts,
                )
            )

    node.create_subscription(LowState, args.lowstate_topic, callback, 50)
    print("joints_observe_only_no_lowcmd_publish=True")
    print(f"lowstate_topic={args.lowstate_topic}")
    print(f"joint_labels={','.join(JOINT_LABELS)}")
    spin_for(node, args.duration)
    node.destroy_node()
    rclpy.shutdown()
    if csv_file is not None:
        csv_file.flush()
        csv_file.close()

    motor_lengths = sorted(int(value) for value in stats["motor_state_lengths"])
    print(f"lowstate_count={int(stats['lowstate_count'])}")
    print(f"valid_joint_sample_count={int(stats['valid_sample_count'])}")
    print(f"motor_state_lengths_seen={motor_lengths if motor_lengths else 'none'}")
    print(f"short_motor_state_count={int(stats['short_motor_state_count'])}")
    print(f"nonfinite_joint_message_count={int(stats['nonfinite_message_count'])}")
    if args.csv:
        print(f"csv_path={args.csv}")

    valid_samples = int(stats["valid_sample_count"])
    ranked = []
    if valid_samples > 0:
        for index, label in enumerate(JOINT_LABELS):
            q_min = float(joint_stats[index]["min"])
            q_max = float(joint_stats[index]["max"])
            ranked.append((q_max - q_min, index, label))
        ranked.sort(key=lambda item: (-item[0], item[1]))

        print("joint_summary_by_range_desc:")
        for joint_range, index, label in ranked:
            joint = joint_stats[index]
            print(
                f"  {index:02d} {label:<8s} "
                f"start={float(joint['start']):+8.4f} "
                f"end={float(joint['end']):+8.4f} "
                f"min={float(joint['min']):+8.4f} "
                f"max={float(joint['max']):+8.4f} "
                f"range={joint_range:8.4f} "
                f"max_abs_dq={float(joint['max_abs_dq']):8.4f}"
            )

        candidates = [label for joint_range, _, label in ranked if joint_range >= args.min_delta]
        print(f"candidate_min_delta_rad={args.min_delta:.4f}")
        print(
            "candidate_changed_joints="
            f"{','.join(candidates) if candidates else 'none'}"
        )
        if not candidates:
            print(
                "NOTE: no joint range exceeded min_delta; this is not a failure for a static "
                "observe-only recording. Move one joint at a time and rerun to validate mapping."
            )

    failures = []
    if int(stats["lowstate_count"]) == 0:
        failures.append(f"no {args.lowstate_topic} messages")
    if int(stats["short_motor_state_count"]) > 0:
        failures.append(
            f"{stats['short_motor_state_count']} messages have motor_state length below "
            f"{len(JOINT_LABELS)}"
        )
    if int(stats["nonfinite_message_count"]) > 0:
        failures.append(f"{stats['nonfinite_message_count']} messages contain NaN/Inf joint q/dq")
    if int(stats["lowstate_count"]) > 0 and valid_samples == 0:
        failures.append("no valid first-12 joint samples were available")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 2
    print("PASS: joint mapping/direction observe-only checks passed; no lowcmd was published")
    return 0


def run_remote(args: argparse.Namespace) -> int:
    ensure_ros_imports()
    rclpy.init()
    node = rclpy.create_node("a2_real_remote_observer")
    count = 0
    invalid_count = 0
    nonzero_seen = False
    last_buttons = None
    mins = {name: float("inf") for name in STICK_NAMES}
    maxs = {name: float("-inf") for name in STICK_NAMES}

    def callback(msg: LowState) -> None:
        nonlocal count, invalid_count, nonzero_seen, last_buttons
        count += 1
        try:
            sticks, buttons, valid = decode_remote(msg.wireless_remote)
        except ValueError as exc:
            invalid_count += 1
            print(f"remote_invalid: {exc}")
            return

        if not valid:
            invalid_count += 1
        for name in STICK_NAMES:
            value = float(sticks[name])
            if math.isfinite(value):
                mins[name] = min(mins[name], value)
                maxs[name] = max(maxs[name], value)
        if any(abs(float(sticks[name])) >= args.deadzone for name in STICK_NAMES if math.isfinite(sticks[name])):
            nonzero_seen = True
        if pressed_button_names(buttons):
            nonzero_seen = True

        button_state = tuple(buttons[name] for name in BUTTON_BYTE2 + BUTTON_BYTE3)
        if button_state != last_buttons:
            display = {
                name: display_stick(float(sticks[name]), args.deadzone)
                for name in STICK_NAMES
            }
            print(
                "remote_change "
                f"tick={int(msg.tick)} "
                f"raw=[lx={sticks['lx']:.3f}, rx={sticks['rx']:.3f}, "
                f"ry={sticks['ry']:.3f}, ly={sticks['ly']:.3f}] "
                f"display=[lx={display['lx']:.3f}, rx={display['rx']:.3f}, "
                f"ry={display['ry']:.3f}, ly={display['ly']:.3f}] "
                f"buttons={format_buttons(buttons)} valid={valid}"
            )
            last_buttons = button_state

    node.create_subscription(LowState, args.lowstate_topic, callback, 50)
    print(f"lowstate_topic={args.lowstate_topic}")
    spin_for(node, args.duration)
    node.destroy_node()
    rclpy.shutdown()

    print(f"remote_lowstate_count={count}")
    print(f"remote_invalid_count={invalid_count}")
    for name in STICK_NAMES:
        if math.isfinite(mins[name]) and math.isfinite(maxs[name]):
            print(f"{name}_range=[{mins[name]:.3f}, {maxs[name]:.3f}]")
        else:
            print(f"{name}_range=unavailable")
    print(f"remote_nonzero_seen={nonzero_seen}")

    failures = []
    if count == 0:
        failures.append(f"no {args.lowstate_topic} messages for remote decode")
    if invalid_count > 0:
        failures.append(f"{invalid_count} remote packets contain invalid stick floats")
    if not args.allow_zero and not nonzero_seen:
        failures.append("remote stayed zero; move sticks or press buttons during the test")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 2
    print("PASS: remote decode checks passed")
    return 0


def run_lowcmd_crc(args: argparse.Namespace) -> int:
    ensure_ros_imports()
    rclpy.init()
    node = rclpy.create_node("a2_real_lowcmd_crc_observer")
    state_seen = False
    latest_mode_machine = None
    lowcmd_count = 0
    crc_failures = 0
    zero_failures = 0
    mode_failures = 0

    def lowstate_callback(msg: LowState) -> None:
        nonlocal state_seen, latest_mode_machine
        state_seen = True
        latest_mode_machine = int(msg.mode_machine)

    def lowcmd_callback(msg: LowCmd) -> None:
        nonlocal lowcmd_count, crc_failures, zero_failures, mode_failures
        lowcmd_count += 1
        computed = compute_lowcmd_crc(msg)
        observed = int(msg.crc) & 0xFFFFFFFF
        crc_ok = observed == computed
        if not crc_ok:
            crc_failures += 1
        zero_ok = True
        if args.expect_zero:
            zero_ok = lowcmd_is_zero(msg)
            if not zero_ok:
                zero_failures += 1
        mode_ok = True
        if args.expect_state_mode and latest_mode_machine is not None:
            mode_ok = int(msg.mode_machine) == latest_mode_machine
            if not mode_ok:
                mode_failures += 1

        if lowcmd_count <= 20 or not (crc_ok and zero_ok and mode_ok):
            state_mode = "unseen" if latest_mode_machine is None else str(latest_mode_machine)
            print(
                f"lowcmd[{lowcmd_count}] "
                f"mode_pr={int(msg.mode_pr)} mode_machine={int(msg.mode_machine)} "
                f"state_mode_machine={state_mode} "
                f"crc=0x{observed:08x} computed=0x{computed:08x} "
                f"crc_ok={crc_ok} zero_ok={zero_ok} mode_ok={mode_ok}"
            )

    node.create_subscription(LowState, args.lowstate_topic, lowstate_callback, 50)
    node.create_subscription(LowCmd, args.lowcmd_topic, lowcmd_callback, 50)
    print(f"lowstate_topic={args.lowstate_topic}")
    print(f"lowcmd_topic={args.lowcmd_topic}")
    spin_for(node, args.duration)
    node.destroy_node()
    rclpy.shutdown()

    print(f"lowcmd_count={lowcmd_count}")
    print(f"state_seen={state_seen}")
    print(f"crc_failures={crc_failures}")
    print(f"zero_failures={zero_failures}")
    print(f"mode_failures={mode_failures}")

    failures = []
    if lowcmd_count == 0:
        failures.append(f"no {args.lowcmd_topic} messages")
    if args.expect_state_mode and not state_seen:
        failures.append(f"no {args.lowstate_topic} observed while checking mode_machine")
    if crc_failures:
        failures.append(f"{crc_failures} LowCmd CRC mismatches")
    if zero_failures:
        failures.append(f"{zero_failures} LowCmd messages are not all-zero STOP commands")
    if mode_failures:
        failures.append(f"{mode_failures} LowCmd mode_machine values differ from latest LowState")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 2
    print("PASS: LowCmd CRC/zero/mode checks passed")
    return 0


def run_no_lowcmd(args: argparse.Namespace) -> int:
    ensure_ros_imports()
    rclpy.init()
    node = rclpy.create_node("a2_real_no_lowcmd_observer")
    count = 0

    def callback(msg: LowCmd) -> None:
        nonlocal count
        count += 1
        print(
            f"unexpected_lowcmd[{count}] "
            f"mode_pr={int(msg.mode_pr)} mode_machine={int(msg.mode_machine)} "
            f"crc=0x{int(msg.crc) & 0xFFFFFFFF:08x}"
        )

    node.create_subscription(LowCmd, args.lowcmd_topic, callback, 50)
    print(f"lowcmd_topic={args.lowcmd_topic}")
    spin_for(node, args.duration)
    node.destroy_node()
    rclpy.shutdown()

    print(f"lowcmd_count={count}")
    if count:
        print(f"FAIL: observed {count} {args.lowcmd_topic} messages", file=sys.stderr)
        return 2
    print(f"PASS: no {args.lowcmd_topic} messages observed")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="A2 real robot ROS2 observers")
    subparsers = parser.add_subparsers(dest="command", required=True)

    lowstate = subparsers.add_parser("lowstate", help="measure lowstate rate and freshness")
    lowstate.add_argument("duration", nargs="?", type=float, default=10.0)
    lowstate.add_argument("--min-hz", type=float, default=50.0)
    lowstate.add_argument("--max-gap-ms", type=float, default=250.0)
    add_lowstate_topic_arg(lowstate)
    lowstate.set_defaults(func=run_lowstate)

    joints = subparsers.add_parser(
        "joints",
        help="observe first-12 lowstate joint q/dq for order and direction validation",
    )
    joints.add_argument("duration", nargs="?", type=float, default=15.0)
    joints.add_argument("--print-period", type=float, default=0.5)
    joints.add_argument("--min-delta", type=float, default=0.03)
    joints.add_argument("--csv")
    add_lowstate_topic_arg(joints)
    joints.set_defaults(func=run_joints)

    remote = subparsers.add_parser("remote", help="decode wireless_remote[40] from lowstate")
    remote.add_argument("duration", nargs="?", type=float, default=15.0)
    remote.add_argument("--deadzone", type=float, default=0.08)
    remote.add_argument("--allow-zero", action="store_true")
    add_lowstate_topic_arg(remote)
    remote.set_defaults(func=run_remote)

    lowcmd_crc = subparsers.add_parser("lowcmd-crc", help="verify lowcmd CRC and safety shape")
    lowcmd_crc.add_argument("duration", nargs="?", type=float, default=8.0)
    lowcmd_crc.add_argument("--expect-zero", action="store_true")
    lowcmd_crc.add_argument("--expect-state-mode", action="store_true")
    add_lowstate_topic_arg(lowcmd_crc)
    add_lowcmd_topic_arg(lowcmd_crc)
    lowcmd_crc.set_defaults(func=run_lowcmd_crc)

    no_lowcmd = subparsers.add_parser("no-lowcmd", help="fail if any lowcmd is observed")
    no_lowcmd.add_argument("duration", nargs="?", type=float, default=8.0)
    add_lowcmd_topic_arg(no_lowcmd)
    no_lowcmd.set_defaults(func=run_no_lowcmd)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
