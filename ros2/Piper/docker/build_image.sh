#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_context="$(cd "$script_dir/.." && pwd)"
image="${PIPER_BRIDGE_IMAGE:-doordog-piper-bridge:humble}"
platform="${PIPER_BRIDGE_PLATFORM:-linux/amd64}"

exec docker build \
  --platform "$platform" \
  --file "$script_dir/Dockerfile" \
  --tag "$image" \
  "$build_context"
