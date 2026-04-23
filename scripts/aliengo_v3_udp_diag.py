#!/usr/bin/env python3
"""
Aliengo v3.0.0 SDK — Raw UDP diagnostic tool (auto-detect motor struct size).

Usage: python3 scripts/aliengo_v3_udp_diag.py
"""

import socket
import struct
import time
import math

TARGET_IP = "192.168.123.10"
TARGET_PORT = 8007
LOCAL_PORT = 8091

# Header: levelFlag(1)+commVersion(2)+robotID(2)+SN(4)+bandWidth(1) = 10
HEADER_FMT = '<B H H I B'
HEADER_SIZE = 10

# IMU: quat[4]+gyro[3]+acc[3]+rpy[3]+temp = 53
IMU_FMT = '<4f 3f 3f 3f b'
IMU_SIZE = 53

# Known tail fields
# footForce[4](8) + footForceEst[4](8) + tick(4) + wirelessRemote[40] + reserve(4) + crc(4) = 68
# But the wire format might differ, so we'll detect motor size first

# LowCmd: safe zero-torque packet
MOTOR_CMD_FMT = '<B f f f f f III'
MOTOR_CMD_SIZE = struct.calcsize(MOTOR_CMD_FMT)
LED_SIZE = 3
LOW_CMD_SIZE = HEADER_SIZE + 20 * MOTOR_CMD_SIZE + 4 * LED_SIZE + 40 + 4 + 4

JOINT_NAMES = [
    "FR_hip", "FR_thigh", "FR_calf",
    "FL_hip", "FL_thigh", "FL_calf",
    "RR_hip", "RR_thigh", "RR_calf",
    "RL_hip", "RL_thigh", "RL_calf",
]


def build_safe_low_cmd():
    buf = bytearray(LOW_CMD_SIZE)
    struct.pack_into(HEADER_FMT, buf, 0, 0xFF, 0, 0, 0, 0)
    off = HEADER_SIZE
    for _ in range(20):
        struct.pack_into(MOTOR_CMD_FMT, buf, off,
                         0x0A, 2.146e9, 16000.0, 0.0, 0.0, 0.0, 0, 0, 0)
        off += MOTOR_CMD_SIZE
    return bytes(buf)


def try_motor_size(data, motor_start, motor_size, n_motors=12):
    """Try parsing motors with a given struct size, return quality score."""
    results = []
    off = motor_start
    good = 0
    for i in range(n_motors):
        if off + 5 > len(data):
            break
        mode = data[off]
        q = struct.unpack_from('<f', data, off + 1)[0]
        dq = struct.unpack_from('<f', data, off + 5)[0]
        results.append((mode, q, dq))
        # Score: mode should be 0x0A(10), q should be finite and in [-3, 3]
        if mode == 10 and math.isfinite(q) and -3.5 < q < 3.5:
            good += 1
        off += motor_size
    return good, results


def detect_motor_struct_size(data):
    """Try motor struct sizes from 26 to 42 and find the best fit."""
    motor_start = HEADER_SIZE + IMU_SIZE  # 63
    best_size = 38
    best_score = 0
    best_results = []

    for msize in range(26, 44):
        remaining = len(data) - motor_start
        if remaining < 20 * msize:
            continue
        score, results = try_motor_size(data, motor_start, msize, 12)
        if score > best_score:
            best_score = score
            best_size = msize
            best_results = results

    return best_size, best_score, best_results


def parse_and_print(data, count, motor_size=None):
    total = len(data)
    print(f"\033[2J\033[H")
    print(f"=== Aliengo v3.0.0 UDP Diag (#{count}) ===")
    print(f"Packet: {total} bytes")

    if total < HEADER_SIZE + IMU_SIZE:
        print(f"ERROR: too small")
        return None

    # Header
    off = 0
    level, cv, rid, sn, bw = struct.unpack_from(HEADER_FMT, data, off)
    off += HEADER_SIZE
    print(f"level=0x{level:02X} commVer={cv} "
          f"robotID={rid} SN={sn}")

    # IMU
    imu = struct.unpack_from(IMU_FMT, data, off)
    off += IMU_SIZE
    q = imu[0:4]
    g = imu[4:7]
    r = imu[10:13]
    print(f"\nIMU quat(w,x,y,z): "
          f"[{q[0]:.4f},{q[1]:.4f},{q[2]:.4f},{q[3]:.4f}]")
    print(f"IMU gyro(rad/s):   "
          f"[{g[0]:.4f},{g[1]:.4f},{g[2]:.4f}]")
    print(f"IMU rpy(rad):      "
          f"[{r[0]:.4f},{r[1]:.4f},{r[2]:.4f}]")

    # Auto-detect motor struct size if not given
    if motor_size is None:
        motor_size, score, _ = detect_motor_struct_size(data)
        print(f"\nAuto-detected motor struct: "
              f"{motor_size} bytes (score={score}/12)")
    else:
        print(f"\nUsing motor struct: {motor_size} bytes")

    # Parse motors
    motor_start = HEADER_SIZE + IMU_SIZE
    print(f"{'Idx':<4} {'Name':<12} {'mode':<5} "
          f"{'q(rad)':<10} {'dq':<10} {'tauEst':<10}")
    print("-" * 55)

    motor_data = []
    for i in range(20):
        moff = motor_start + i * motor_size
        if moff + 9 > total:
            break
        mode = data[moff]
        mq = struct.unpack_from('<f', data, moff + 1)[0]
        mdq = struct.unpack_from('<f', data, moff + 5)[0]
        # ddq at +9, tauEst at +13 (if struct is big enough)
        tau = 0.0
        if moff + 17 <= total:
            tau = struct.unpack_from('<f', data, moff + 13)[0]

        name = JOINT_NAMES[i] if i < 12 else f"m{i}"
        if i < 12:
            print(f"{i:<4} {name:<12} {mode:<5} "
                  f"{mq:<10.4f} {mdq:<10.4f} {tau:<10.3f}")
            motor_data.append({
                'mode': mode, 'q': mq, 'dq': mdq, 'tau': tau
            })

    # Tail region
    tail_start = motor_start + 20 * motor_size
    tail_bytes = total - tail_start
    print(f"\nTail: {tail_bytes} bytes "
          f"(from offset {tail_start})")

    if tail_bytes >= 8:
        ff = struct.unpack_from('<4h', data, tail_start)
        print(f"footForce: {list(ff)}")

    # WirelessRemote: try to find it
    # In tail: footForce(8)+footForceEst(8)+tick(4)=20, then 40 bytes remote
    wr_offset = tail_start + 20
    if wr_offset + 40 <= total:
        wr = data[wr_offset:wr_offset + 40]
        keys = struct.unpack_from('<H', wr, 2)[0]
        lx = struct.unpack_from('<f', wr, 4)[0]
        ly = struct.unpack_from('<f', wr, 20)[0]
        rx = struct.unpack_from('<f', wr, 8)[0]
        print(f"Remote keys=0x{keys:04X} "
              f"lx={lx:.3f} ly={ly:.3f} rx={rx:.3f}")

    return motor_size


def main():
    print(f"Connecting to {TARGET_IP}:{TARGET_PORT}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    sock.bind(('0.0.0.0', LOCAL_PORT))

    cmd = build_safe_low_cmd()
    count = 0
    detected_size = None

    try:
        while True:
            sock.sendto(cmd, (TARGET_IP, TARGET_PORT))
            try:
                data, _ = sock.recvfrom(4096)
                count += 1
                if count <= 5 or count % 50 == 0:
                    detected_size = parse_and_print(
                        data, count, detected_size)
            except socket.timeout:
                print(f"[{count}] TIMEOUT")
            time.sleep(0.02)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == '__main__':
    main()
