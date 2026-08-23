#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
docker_dir="${deploy_dir}/docker"
env_file="${docker_dir}/.env"

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: missing ${env_file}; run scripts/configure_policy_host.sh first." >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a

compose_args=(
  --env-file "${env_file}"
  -f "${docker_dir}/compose.yaml"
)

case "${1:-}" in
  "")
    ;;
  --cuda)
    echo "ERROR: CUDA Stage2 image build is disabled." >&2
    echo "Reason: no matching cxx11-ABI LibTorch 2.7 CUDA artifact has been selected for the ROS2 C++ runtime." >&2
    echo "Use the CPU path: $0" >&2
    exit 2
    ;;
  *)
    echo "Usage: $0" >&2
    echo "CUDA is disabled until a matching cxx11-ABI LibTorch 2.7 artifact is selected." >&2
    exit 2
    ;;
esac

: "${A2_BASE_IMAGE:?Missing A2_BASE_IMAGE in docker/.env}"
: "${A2_DOCKER_PLATFORM:?Missing A2_DOCKER_PLATFORM in docker/.env}"

if ! docker image inspect "${A2_BASE_IMAGE}" >/dev/null 2>&1; then
  echo "[stage2-build] base image missing; building ${A2_BASE_IMAGE} first."
  A2_DOCKER_IMAGE="${A2_BASE_IMAGE}" \
  A2_DOCKER_PLATFORM="${A2_DOCKER_PLATFORM}" \
    "${deploy_dir}/../../ros2/A2/docker/build_image.sh"
fi

docker compose "${compose_args[@]}" build policy-runtime
echo "[PASS] built Stage2 image: ${A2_PIPER_STAGE2_IMAGE}"
