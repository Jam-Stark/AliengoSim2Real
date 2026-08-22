#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_context="$(cd "$script_dir/.." && pwd)"
image="${PIPER_BRIDGE_IMAGE:-doordog-piper-bridge:humble}"
platform_args=()
if [ -n "${PIPER_BRIDGE_PLATFORM:-}" ]; then
  platform_args=(--platform "$PIPER_BRIDGE_PLATFORM")
fi

exec docker build \
  "${platform_args[@]}" \
  --file "$script_dir/Dockerfile" \
  --tag "$image" \
  "$build_context"
