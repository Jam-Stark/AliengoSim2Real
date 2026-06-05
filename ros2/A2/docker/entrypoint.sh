#!/usr/bin/env bash
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -e
fi

source_if_present() {
  local setup_file="$1"
  if [ -f "$setup_file" ]; then
    # shellcheck disable=SC1090
    source "$setup_file"
  fi
}

source_if_present /opt/ros/humble/setup.bash
source_if_present /opt/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash
source_if_present /work/projects/AliengoSim2Real/ros2/install/setup.bash

export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export A2_NET_IFACE="${A2_NET_IFACE:-lo}"
export Torch_DIR="${Torch_DIR:-/opt/libtorch/share/cmake/Torch}"
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH:-}"
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH:-}"
export PATH="/opt/unitree/unitree_sdk2/install/bin:/opt/unitree/unitree_sdk2/build/bin:${PATH}"

export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${A2_NET_IFACE}\" priority=\"default\" multicast=\"default\" /></Interfaces></General></Domain></CycloneDDS>"

if [ "${A2_ENTRYPOINT_QUIET:-0}" != "1" ]; then
  echo "[a2-entrypoint] RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
  echo "[a2-entrypoint] A2_NET_IFACE=${A2_NET_IFACE}"
  if [ "${A2_NET_IFACE}" = "lo" ]; then
    echo "[a2-entrypoint] Using loopback. Set A2_NET_IFACE=enp131s0 for real A2 hardware."
  fi
fi

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  exec "$@"
fi
