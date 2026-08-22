#!/usr/bin/env bash
set -euo pipefail

source /opt/ros/humble/setup.bash
source /opt/piper_ws/install/setup.bash

export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export ROS_LOCALHOST_ONLY=0
export PIPER_NET_IFACE="${PIPER_NET_IFACE:-}"
export PIPER_CAN_NAME="${PIPER_CAN_NAME:-can0}"
export PIPER_NAMESPACE="${PIPER_NAMESPACE:-piper}"

if [ -z "$PIPER_NET_IFACE" ]; then
  echo "PIPER_NET_IFACE is required and must be the PC2 interface on 192.168.123.0/24" >&2
  exit 2
fi

export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${PIPER_NET_IFACE}\" priority=\"default\" multicast=\"default\" /></Interfaces></General></Domain></CycloneDDS>"

if [ "${1:-}" = "__bridge__" ]; then
  ip link show "$PIPER_CAN_NAME" >/dev/null
  exec ros2 run piper_bridge piper_bridge --ros-args \
    -r "__ns:=/${PIPER_NAMESPACE}" \
    --params-file /opt/piper_ws/install/piper_bridge/share/piper_bridge/config/piper_bridge.yaml \
    -p "can_name:=${PIPER_CAN_NAME}"
fi

exec "$@"
