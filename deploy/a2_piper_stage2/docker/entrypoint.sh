#!/usr/bin/env bash
set -e

source_if_present() {
  local setup_file="$1"
  if [[ -f "${setup_file}" ]]; then
    # shellcheck disable=SC1090
    source "${setup_file}"
  fi
}

# Reuse the base image's tested ROS/Unitree environment, then put the PyTorch
# 2.7 wheel and the Stage2 overlay first.
export A2_ENTRYPOINT_QUIET=1
source_if_present /opt/a2/entrypoint.sh
source_if_present /opt/stage2_ws/install/setup.bash

export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export A2_NET_IFACE="${A2_NET_IFACE:?A2_NET_IFACE must be configured}"
export ROS_NETWORK_INTERFACE="${ROS_NETWORK_INTERFACE:-${A2_NET_IFACE}}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:?ROS_DOMAIN_ID must be configured}"
export Torch_DIR=/opt/libtorch-stage2/share/cmake/Torch
export CMAKE_PREFIX_PATH="/opt/libtorch-stage2:/opt/stage2_ws/install:${CMAKE_PREFIX_PATH:-}"
export LD_LIBRARY_PATH="/opt/libtorch-stage2/lib:/opt/stage2_ws/install/lib:${LD_LIBRARY_PATH:-}"
export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${ROS_NETWORK_INTERFACE}\" priority=\"default\" multicast=\"default\" /></Interfaces></General></Domain></CycloneDDS>"

if [[ "${STAGE2_ENTRYPOINT_QUIET:-0}" != "1" ]]; then
  echo "[stage2-entrypoint] iface=${ROS_NETWORK_INTERFACE} domain=${ROS_DOMAIN_ID} rmw=${RMW_IMPLEMENTATION}"
  echo "[stage2-entrypoint] Torch_DIR=${Torch_DIR}"
fi

exec "$@"
