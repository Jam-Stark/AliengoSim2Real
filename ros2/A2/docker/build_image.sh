#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
image="${A2_DOCKER_IMAGE:-a2-humble-deploy:2026-06-05}"
platform="${A2_DOCKER_PLATFORM:-linux/amd64}"

docker build \
  --platform "$platform" \
  -t "$image" \
  -f "$script_dir/Dockerfile" \
  "$@" \
  "$script_dir"

echo "[a2-docker] built image: $image ($platform)"
