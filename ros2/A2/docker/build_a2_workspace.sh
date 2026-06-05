#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  build_a2_workspace.sh [--lowlevel-only|--policy] [--cmake-release] [--symlink-install]

Run inside the A2 Humble Docker container. It builds from:
  /work/projects/AliengoSim2Real/ros2

Modes:
  --lowlevel-only   Build a2_lowlevel adapter and smoke only. This is default.
  --policy          Build optional a2_policy_deploy with CPU LibTorch.

Options:
  --cmake-release   Pass -DCMAKE_BUILD_TYPE=Release.
  --symlink-install Use colcon --symlink-install.
USAGE
}

build_policy=0
cmake_release=0
symlink_install=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --lowlevel-only)
      build_policy=0
      ;;
    --policy)
      build_policy=1
      ;;
    --cmake-release)
      cmake_release=1
      ;;
    --symlink-install)
      symlink_install=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

source_if_present() {
  local setup_file="$1"
  if [ -f "$setup_file" ]; then
    # shellcheck disable=SC1090
    source "$setup_file"
  fi
}

source_if_present /opt/ros/humble/setup.bash
source_if_present /opt/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash

workspace=/work/projects/AliengoSim2Real/ros2
if [ ! -d "$workspace" ]; then
  echo "ERROR: workspace not found: $workspace" >&2
  exit 1
fi

cd "$workspace"

colcon_args=(build --packages-select a2_lowlevel)
if [ "$symlink_install" -eq 1 ]; then
  colcon_args+=(--symlink-install)
fi

cmake_args=(-DBUILD_TESTING=OFF)
if [ "$cmake_release" -eq 1 ]; then
  cmake_args+=(-DCMAKE_BUILD_TYPE=Release)
fi
if [ "$build_policy" -eq 1 ]; then
  cmake_args+=(
    -DBUILD_A2_POLICY_DEPLOY=ON
    -DTorch_DIR=/opt/libtorch/share/cmake/Torch
  )
fi

echo "[a2-build] workspace=$workspace"
echo "[a2-build] mode=$([ "$build_policy" -eq 1 ] && echo policy || echo lowlevel-only)"
echo "[a2-build] colcon ${colcon_args[*]} --cmake-args ${cmake_args[*]}"

colcon "${colcon_args[@]}" --cmake-args "${cmake_args[@]}"
