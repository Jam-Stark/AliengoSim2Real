#!/usr/bin/env python3
"""
Aliengo v3.0.0 SDK — Raw UDP diagnostic tool.

Directly sends LowCmd packets and parses LowState responses
using the Aliengo v3.0.0 struct layout (from TX2 comm.h).
No SDK .so dependency — pure Python struct parsing.

Usage (in Docker container or any machine on 192.168.123.x):
  python3 scripts/aliengo_v3_udp_diag.py

Press Ctrl+C to stop.
"""

import socket
import struct
import time
import sys

# ============================================================
# Aliengo v3.0.0 struct layout (#pragma pack(1))
# ============================================================

# --- MotorState (38 bytes) ---
# uint8_t mode(1) + float q(4) + float dq(4) + float ddq(4) +
# float tauEst(4) + float q_raw(4) + float dq_raw(4) + float ddq_raw(4) +
# int8_t temperature(1) + uint32_t reserve[2](8)
MOTOR_STATE_FMT = '<B f f f f f f f b II'
MOTOR_STATE_SIZE = struct.calcsize(MOTOR_STATE_FMT)  # should be 38

# --- IMU (53 bytes) ---
# float quaternion[4](16) + float gyroscope[3](12) + float accelerometer[3](12) +
# float rpy[3](12) + int8_t temperature(1)
IMU_FMT = '<4f 3f 3f 3f b'
IMU_SIZE = struct.calcsize(IMU_FMT)  # should be 53

# --- LowState header (10 bytes) ---
# uint8_t levelFlag(1) + uint16_t commVersion(2) + uint16_t robotID(2) +
# uint32_t SN(4) + uint8_t bandWidth(1)
HEADER_FMT = '<B H H I B'
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # should be 10

# --- LowState tail (after motors) ---
# int16_t footForce[4](8) + int16_t footForceEst[4](8) + uint32_t tick(4) +
# uint8_t wirelessRemote[40](40) + uint32_t reserve(4) + uint32_t crc(4)
TAIL_FMT = '<4h 4h I 40B I I'
TAIL_SIZE = struct.calcsize(TAIL_FMT)  # should be 68

# Total expected sizeof(LowState)
EXPECTED_LOW_STATE_SIZE = HEADER_SIZE + IMU_SIZE + 20 * MOTOR_STATE_SIZE + TAIL_SIZE

# --- MotorCmd (33 bytes) ---
# uint8_t mode(1) + float q(4) + float dq(4) + float tau(4) +
# float Kp(4) + float Kd(4) + uint32_t reserve[3](12)
MOTOR_CMD_FMT = '<B f f f f f III'
MOTOR_CMD_SIZE = struct.calcsize(MOTOR_CMD_FMT)  # should be 33

# --- LED (3 bytes) ---
LED_FMT = '<BBB'
LED_SIZE = 3

# --- LowCmd ---
# header(10) + MotorCmd[20] + LED[4] + wirelessRemote[40] + reserve(4) + crc(4)
LOW_CMD_SIZE = HEADER_SIZE + 20 * MOTOR_CMD_SIZE + 4 * LED_SIZE + 40 + 4 + 4

# Joint names (SDK order)
JOINT_NAMES = [
    "FR_hip", "FR_thigh", "FR_calf",
    "FL_hip", "FL_thigh", "FL_calf",
    "RR_hip", "RR_thigh", "RR_calf",
    "RL_hip", "RL_thigh", "RL_calf",
]

TARGET_IP = "192.168.123.10"
TARGET_PORT = 8007
LOCAL_PORT = 8091


def build_safe_low_cmd():
    """Build a safe zero-torque LowCmd packet."""
    buf = bytearray(LOW_CMD_SIZE)
    # Header
    struct.pack_into(HEADER_FMT, buf, 0, 0xFF, 0, 0, 0, 0)  # levelFlag=LOWLEVEL
    # MotorCmd[20]: mode=0x0A, q=PosStopF, dq=VelStopF, rest=0
    offset = HEADER_SIZE
    for _ in range(20):
        struct.pack_into(MOTOR_CMD_FMT, buf, offset,
                         0x0A,           # mode
                         2.146e9,        # q = PosStopF
                         16000.0,        # dq = VelStopF
                         0.0, 0.0, 0.0,  # tau, Kp, Kd
                         0, 0, 0)        # reserve
        offset += MOTOR_CMD_SIZE
    return bytes(buf)


def parse_low_state(data):
    """Parse raw bytes into LowState fields. Tries v3.0.0 struct layout."""
    result = {}
    total = len(data)
    result['raw_size'] = total

    if total < HEADER_SIZE + IMU_SIZE:
        result['error'] = f'Packet too small: {total} bytes'
        return result

    # Header
    off = 0
    level, comm_ver, robot_id, sn, bw = struct.unpack_from(HEADER_FMT, data, off)
    off += HEADER_SIZE
    result['levelFlag'] = level
    result['commVersion'] = comm_ver
    result['robotID'] = robot_id
    result['SN'] = sn

    # IMU
    imu_vals = struct.unpack_from(IMU_FMT, data, off)
    off += IMU_SIZE
    result['imu'] = {
        'quaternion': list(imu_vals[0:4]),
        'gyroscope': list(imu_vals[4:7]),
        'accelerometer': list(imu_vals[7:10]),
        'rpy': list(imu_vals[10:13]),
        'temperature': imu_vals[13],
    }

    # MotorState[20] — try to parse, but handle size mismatch
    motors = []
    for i in range(20):
        if off + MOTOR_STATE_SIZE > total:
            result['motor_parse_error'] = f'Ran out of bytes at motor {i}, offset {off}'
            break
        vals = struct.unpack_from(MOTOR_STATE_FMT, data, off)
        off += MOTOR_STATE_SIZE
        motors.append({
            'mode': vals[0],
            'q': vals[1],
            'dq': vals[2],
            'ddq': vals[3],
            'tauEst': vals[4],
            'temperature': vals[8],
        })
    result['motors'] = motors

    # Tail
    if off + TAIL_SIZE <= total:
        tail = struct.unpack_from(TAIL_FMT, data, off)
        off += TAIL_SIZE
        result['footForce'] = list(tail[0:4])
        result['footForceEst'] = list(tail[4:8])
        result['tick'] = tail[8]
        result['wirelessRemote'] = list(tail[9:49])
        result['crc'] = tail[50]
    else:
        result['tail_error'] = f'Not enough bytes for tail at offset {off}, have {total}'

    result['bytes_consumed'] = off
    result['bytes_remaining'] = total - off
    return result


def print_state(state, count):
    """Pretty print parsed LowState."""
    print(f"\033[2J\033[H")  # clear screen
    print(f"=== Aliengo v3.0.0 Raw UDP Diagnostic (packet #{count}) ===")
    print(f"Raw packet size: {state['raw_size']} bytes  "
          f"(expected sizeof: {EXPECTED_LOW_STATE_SIZE})")
    if 'error' in state:
        print(f"ERROR: {state['error']}")
        return

    print(f"levelFlag: 0x{state['levelFlag']:02X}  "
          f"commVersion: {state['commVersion']}  "
          f"robotID: {state['robotID']}  SN: {state['SN']}")

    imu = state['imu']
    q = imu['quaternion']
    print(f"\nIMU quaternion (w,x,y,z): [{q[0]:.4f}, {q[1]:.4f}, {q[2]:.4f}, {q[3]:.4f}]")
    g = imu['gyroscope']
    print(f"IMU gyroscope (rad/s):    [{g[0]:.4f}, {g[1]:.4f}, {g[2]:.4f}]")
    r = imu['rpy']
    print(f"IMU rpy (rad):            [{r[0]:.4f}, {r[1]:.4f}, {r[2]:.4f}]")
    print(f"IMU temperature: {imu['temperature']}°C")

    motors = state.get('motors', [])
    print(f"\nMotors ({len(motors)} parsed):")
    print(f"  {'Index':<6} {'Name':<12} {'mode':<6} {'q(rad)':<10} {'dq(rad/s)':<10} {'tauEst':<10} {'temp':<6}")
    print(f"  {'-'*60}")
    for i, m in enumerate(motors[:12]):
        name = JOINT_NAMES[i] if i < 12 else f"motor_{i}"
        print(f"  {i:<6} {name:<12} {m['mode']:<6} {m['q']:<10.4f} {m['dq']:<10.4f} "
              f"{m['tauEst']:<10.3f} {m['temperature']:<6}")

    if 'motor_parse_error' in state:
        print(f"\n  ⚠ {state['motor_parse_error']}")

    if 'footForce' in state:
        ff = state['footForce']
        print(f"\nFoot force: {ff}")
    if 'tick' in state:
        print(f"Tick: {state['tick']} us")

    if 'tail_error' in state:
        print(f"\n⚠ {state['tail_error']}")

    print(f"\nBytes consumed: {state.get('bytes_consumed', '?')} / {state['raw_size']}")
    if state.get('bytes_remaining', 0) != 0:
        print(f"⚠ {state['bytes_remaining']} bytes remaining (struct size mismatch)")


def main():
    print(f"MotorState size: {MOTOR_STATE_SIZE} bytes")
    print(f"IMU size: {IMU_SIZE} bytes")
    print(f"Header size: {HEADER_SIZE} bytes")
    print(f"Tail size: {TAIL_SIZE} bytes")
    print(f"Expected sizeof(LowState): {EXPECTED_LOW_STATE_SIZE} bytes")
    print(f"LowCmd size: {LOW_CMD_SIZE} bytes")
    print(f"\nConnecting to {TARGET_IP}:{TARGET_PORT} from local port {LOCAL_PORT}...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    sock.bind(('0.0.0.0', LOCAL_PORT))

    cmd = build_safe_low_cmd()
    count = 0

    try:
        while True:
            sock.sendto(cmd, (TARGET_IP, TARGET_PORT))
            try:
                data, addr = sock.recvfrom(4096)
                count += 1
                state = parse_low_state(data)
                if count % 50 == 0 or count <= 3:  # print every ~1 sec at 50Hz
                    print_state(state, count)
            except socket.timeout:
                print(f"[{count}] TIMEOUT")
            time.sleep(0.02)  # ~50 Hz
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == '__main__':
    main()
