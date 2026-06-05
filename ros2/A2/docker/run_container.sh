#!/usr/bin/env bash
set -euo pipefail

image="${A2_DOCKER_IMAGE:-a2-humble-deploy:2026-06-05}"
platform="${A2_DOCKER_PLATFORM:-linux/amd64}"
container_name="${A2_CONTAINER_NAME:-a2-humble}"
host_projects_dir="${HOST_PROJECTS_DIR:-/home/baoquanc/Downloads/WorkSpace/projects}"
container_projects_dir="${CONTAINER_PROJECTS_DIR:-/work/projects}"
a2_net_iface="${A2_NET_IFACE:-lo}"

if [ ! -d "$host_projects_dir" ]; then
  echo "WARN: HOST_PROJECTS_DIR does not exist on this host: $host_projects_dir" >&2
fi

if [ "$a2_net_iface" = "lo" ]; then
  echo "WARN: A2_NET_IFACE is lo. Use A2_NET_IFACE=enp131s0 for real A2 hardware." >&2
fi

exec docker run -it --rm \
  --platform "$platform" \
  --name "$container_name" \
  --hostname "$container_name" \
  --network host \
  --privileged \
  --ipc host \
  -e A2_NET_IFACE="$a2_net_iface" \
  -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
  -e HOST_PROJECTS_DIR="$host_projects_dir" \
  -v "$host_projects_dir:$container_projects_dir" \
  -v "$HOME/.ros:/root/.ros" \
  -w "$container_projects_dir/AliengoSim2Real/ros2" \
  "$image" \
  "$@"
