#!/usr/bin/env bash
set -euo pipefail

workspace_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_file="$workspace_root/docker/compose.yaml"
env_file="$workspace_root/docker/piper_bridge.env"
compose=(docker compose --env-file "$env_file" -f "$compose_file")

case "${1:-}" in
  start)
    ip -details link show can_piper
    "${compose[@]}" up -d
    ;;
  stop)
    "${compose[@]}" stop
    ;;
  restart)
    "${compose[@]}" stop
    ip -details link show can_piper
    "${compose[@]}" up -d
    ;;
  status)
    "${compose[@]}" ps
    ;;
  logs)
    "${compose[@]}" logs --tail 200 -f piper-bridge
    ;;
  *)
    echo "Usage: $0 {start|stop|restart|status|logs}" >&2
    exit 2
    ;;
esac
