#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
deploy_info_primary="$repo_root/ros2/A2/DeployMachineINFO.md"
deploy_info_fallback="$repo_root/DeployMachineINFO.md"
deploy_info="$deploy_info_primary"
if [ ! -f "$deploy_info" ] && [ -f "$deploy_info_fallback" ]; then
  deploy_info="$deploy_info_fallback"
fi
image="${A2_DOCKER_IMAGE:-a2-humble-deploy:2026-06-05}"
platform="${A2_DOCKER_PLATFORM:-linux/amd64}"
host_projects_dir="${HOST_PROJECTS_DIR:-/home/baoquanc/Downloads/WorkSpace/projects}"
iface="${A2_NET_IFACE:-}"
run_ping=0
container_check=0

usage() {
  cat <<'USAGE'
Usage:
  preflight.sh [--iface IFACE] [--ping] [--container-check] [--image IMAGE] [--platform PLATFORM]

Checks Docker, candidate A2 network interface, 192.168.123.x subnet readiness,
optional ping to 192.168.123.161, and optional inside-container Unitree/ROS2
package readiness. It never modifies host networking.

Docker default platform is A2_DOCKER_PLATFORM=linux/amd64 because the formal
A2 deploy machine target is x86_64. Override only for debugging.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --iface)
      iface="${2:-}"
      shift
      ;;
    --ping)
      run_ping=1
      ;;
    --container-check)
      container_check=1
      ;;
    --image)
      image="${2:-}"
      shift
      ;;
    --platform)
      platform="${2:-}"
      shift
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

section() {
  printf '\n## %s\n' "$1"
}

command_exists() {
  command -v "$1" >/dev/null 2>&1
}

section "Deploy Info Facts"
if [ -f "$deploy_info" ]; then
  echo "deploy info: $deploy_info"
  grep -E '^- (timestamp|hostname|user|cwd|unitree root):' "$deploy_info" || true
  grep -E '^(PRETTY_NAME=|VERSION_ID=|VERSION_CODENAME=)' "$deploy_info" || true
else
  echo "MISSING: $deploy_info_primary"
  echo "fallback also missing: $deploy_info_fallback"
fi

section "Docker"
echo "image: $image"
echo "platform: $platform"
if command_exists docker; then
  docker --version
  if docker image inspect "$image" >/dev/null 2>&1; then
    echo "image: FOUND $image"
  else
    echo "image: MISSING $image"
    echo "build command: A2_DOCKER_PLATFORM=$platform bash ros2/A2/docker/build_image.sh"
  fi
else
  echo "MISSING: docker"
fi

section "A2 Network Interface"
if [ -z "$iface" ]; then
  if command_exists ip && ip link show enp131s0 >/dev/null 2>&1; then
    iface=enp131s0
  elif command_exists ip; then
    iface="$(ip -o -4 addr show | awk '/192\.168\.123\./ {print $2; exit}')"
  fi
fi
iface="${iface:-lo}"
echo "candidate iface: $iface"

if command_exists ip; then
  ip -brief link show "$iface" 2>/dev/null || echo "MISSING iface: $iface"
  ip -4 addr show "$iface" 2>/dev/null || true
  if ip -4 addr show "$iface" 2>/dev/null | grep -q '192\.168\.123\.'; then
    echo "A2 low-level subnet: FOUND 192.168.123.x on $iface"
  else
    echo "A2 low-level subnet: MISSING 192.168.123.x on $iface"
    cat <<'NETWORK'
Manual host setup example, if enp131s0 is the robot Ethernet NIC:
  sudo ip link set enp131s0 up
  sudo ip addr flush dev enp131s0
  sudo ip addr add 192.168.123.99/24 dev enp131s0

The 192.168.124.x subnet is not the SDK2 low-level control subnet for this
A2 path; use 192.168.123.x for rt/lowstate and rt/lowcmd DDS traffic.
NETWORK
  fi
else
  echo "MISSING: ip command"
fi

section "Optional Ping"
if [ "$run_ping" -eq 1 ]; then
  if command_exists ping; then
    ping -c 2 -W 1 192.168.123.161 || true
  else
    echo "MISSING: ping"
  fi
else
  echo "SKIPPED: pass --ping to test 192.168.123.161"
fi

section "Workspace Mount"
echo "HOST_PROJECTS_DIR=${host_projects_dir}"
if [ -d "$host_projects_dir/AliengoSim2Real" ]; then
  echo "AliengoSim2Real mount source: FOUND"
else
  echo "AliengoSim2Real mount source: MISSING"
fi

section "Container ROS2/Unitree Readiness"
if [ "$container_check" -eq 1 ]; then
  if ! command_exists docker; then
    echo "SKIPPED: docker missing"
  elif ! docker image inspect "$image" >/dev/null 2>&1; then
    echo "SKIPPED: image missing: $image"
  else
    docker run --rm \
      --platform "$platform" \
      --network host \
      --privileged \
      --ipc host \
      -e A2_NET_IFACE="$iface" \
      -e A2_ENTRYPOINT_QUIET=1 \
      -v "$host_projects_dir:/work/projects" \
      "$image" \
      bash -lc '
        set -e
        echo "ROS_DISTRO=${ROS_DISTRO:-UNSET}"
        echo "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-UNSET}"
        ros2 pkg prefix unitree_hg
        ros2 pkg prefix unitree_go
        ros2 pkg prefix unitree_api
        ros2 interface show unitree_hg/msg/LowState >/dev/null
        ros2 interface show unitree_hg/msg/LowCmd >/dev/null
        echo "Unitree ROS2 message interfaces: OK"
        if [ -d /work/projects/AliengoSim2Real/ros2/install ]; then
          ros2 pkg prefix a2_lowlevel || true
        else
          echo "A2 workspace install: MISSING; run /opt/a2/build_a2_workspace.sh"
        fi
      '
  fi
else
  echo "SKIPPED: pass --container-check after building the Docker image"
fi
