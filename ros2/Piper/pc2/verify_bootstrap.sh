#!/usr/bin/env bash
set -euo pipefail

workspace_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="a2-piper-pc2-bridge:humble-20260824"

docker version
docker compose version
docker info --format 'server={{.ServerVersion}} driver={{.Driver}}'
docker image inspect "$image" \
  --format 'image={{index .RepoTags 0}} os={{.Os}} arch={{.Architecture}}'
docker compose \
  --env-file "$workspace_root/docker/piper_bridge.env" \
  -f "$workspace_root/docker/compose.yaml" \
  config --quiet

command -v candump
systemctl is-active nginx

if docker ps --format '{{.Names}}' | grep -Fxq a2-piper-pc2-bridge; then
  echo "PiPER bridge container is unexpectedly running." >&2
  exit 1
fi

ip link show can0 | grep -F 'state DOWN'
ip -details link show can0 | grep -F 'can state STOPPED'
if ip link show can_piper >/dev/null 2>&1; then
  echo "can_piper unexpectedly exists before CAN authorization." >&2
  exit 1
fi

echo "PASS: PC2 Docker/bridge bootstrap is installed; bridge stopped; CAN unchanged."
