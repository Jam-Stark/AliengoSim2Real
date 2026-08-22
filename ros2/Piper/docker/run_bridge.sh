#!/usr/bin/env bash
set -euo pipefail

image="${PIPER_BRIDGE_IMAGE:-doordog-piper-bridge:humble}"
platform="${PIPER_BRIDGE_PLATFORM:-linux/amd64}"
net_iface="${PIPER_NET_IFACE:-}"
can_name="${PIPER_CAN_NAME:-can0}"
namespace="${PIPER_NAMESPACE:-piper}"
ros_domain_id="${ROS_DOMAIN_ID:-0}"

if [ -z "$net_iface" ]; then
  echo "Set PIPER_NET_IFACE to the PC2 interface carrying 192.168.123.162." >&2
  exit 2
fi
ip link show "$net_iface" >/dev/null
ip link show "$can_name" >/dev/null

exec docker run --rm -it \
  --platform "$platform" \
  --network host \
  -e "PIPER_NET_IFACE=$net_iface" \
  -e "PIPER_CAN_NAME=$can_name" \
  -e "PIPER_NAMESPACE=$namespace" \
  -e "ROS_DOMAIN_ID=$ros_domain_id" \
  "$image"
