#!/usr/bin/env bash

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  echo "Source this file: source scripts/use_ros2_interface.sh <network-interface>" >&2
  exit 2
fi

iface="${1:-}"
if [ -z "$iface" ]; then
  echo "network interface is required" >&2
  return 2
fi
if ! ip link show "$iface" >/dev/null 2>&1; then
  echo "network interface not found: $iface" >&2
  return 2
fi

export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}"
export ROS_LOCALHOST_ONLY=0
export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${iface}\" priority=\"default\" multicast=\"default\" /></Interfaces></General></Domain></CycloneDDS>"

echo "ROS 2 DDS bound to $iface (ROS_DOMAIN_ID=$ROS_DOMAIN_ID)"
