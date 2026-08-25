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

# The base image exports its C++ LibTorch 2.1 directory, while Stage2 builds
# against the separate C++ LibTorch 2.7 distribution. Neither may shadow the
# Python Torch wheel's private libraries. C++ executables carry the Stage2
# LibTorch install RPATH; retain the ROS/Unitree/workspace library paths only.
stage2_ld_library_path=""
IFS=: read -r -a stage2_library_dirs <<< "${LD_LIBRARY_PATH:-}"
for stage2_library_dir in "${stage2_library_dirs[@]}"; do
  case "${stage2_library_dir}" in
    ""|/opt/libtorch/lib|/opt/libtorch-stage2/lib) continue ;;
  esac
  if [[ -z "${stage2_ld_library_path}" ]]; then
    stage2_ld_library_path="${stage2_library_dir}"
  else
    stage2_ld_library_path="${stage2_ld_library_path}:${stage2_library_dir}"
  fi
done
export LD_LIBRARY_PATH="/opt/stage2_ws/install/lib:${stage2_ld_library_path}"
export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${ROS_NETWORK_INTERFACE}\" priority=\"default\" multicast=\"default\" /></Interfaces></General></Domain></CycloneDDS>"

if [[ "${STAGE2_ENTRYPOINT_QUIET:-0}" != "1" ]]; then
  echo "[stage2-entrypoint] iface=${ROS_NETWORK_INTERFACE} domain=${ROS_DOMAIN_ID} rmw=${RMW_IMPLEMENTATION}"
  echo "[stage2-entrypoint] Torch_DIR=${Torch_DIR}"
fi

exec "$@"
