#!/usr/bin/env bash
set -u

section() {
  printf '\n## %s\n\n' "$1"
}
run() {
  printf '\n```text\n$ %s\n' "$*"
  "$@" 2>&1 || true
  printf '```\n'
}

echo "# A2 PC2 PiPER bridge environment report"
echo
printf 'Collected: %s\n' "$(date --iso-8601=seconds 2>/dev/null || date)"

section "Host"
run hostnamectl
run uname -a
run bash -lc 'cat /etc/os-release'
run python3 --version
run bash -lc 'command -v docker && docker --version'

section "Network"
run ip -br link
run ip -br address
run ip route
run bash -lc 'command -v ros2 && env | grep -E "^(ROS_|RMW_|CYCLONEDDS)" | sort'

section "USB and SocketCAN"
run lsusb
run bash -lc 'ip -details link show type can'
run bash -lc 'command -v candump && command -v cansend && command -v ethtool'

section "ROS 2 and Python packages"
run bash -lc 'ls -1 /opt/ros 2>/dev/null'
run python3 -c 'import sys; print(sys.executable); print(sys.version)'
run python3 -c 'import can; print("python-can", can.__version__)'
run python3 -c 'import piper_sdk; print("piper_sdk", piper_sdk.__file__)'
run python3 -c 'from piper_sdk import C_PiperInterface_V2; print("velocity_window", hasattr(C_PiperInterface_V2, "GetArmHighSpdInfoAverage"))'

section "Relevant processes"
run bash -lc 'ps -eo pid,comm,args | grep -E "(unitree|slam|ros2|dds|piper)" | grep -v grep | head -100'
