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
: "${EVIDENCE_DIR:?Missing EVIDENCE_DIR in docker/.env}"

compose=(
  docker compose
  --env-file "${env_file}"
  -f "${docker_dir}/compose.yaml"
)
cli=(python3 -m a2_piper_stage2_deploy.cli)
mode="${1:-mock}"
session="${EVIDENCE_SESSION:-$(date +%Y%m%d_%H%M%S)_shadow_${mode}}"
session_dir="${EVIDENCE_DIR}/${session}"
mkdir -p "${session_dir}"

run_cli() {
  local label="$1"
  shift
  echo "[stage2-shadow] ${label}"
  "${compose[@]}" run --rm --no-deps -T policy-runtime "${cli[@]}" "$@" \
    2>&1 | tee "${session_dir}/${label}.log"
}

run_direct_shadow() {
  local duration="$1"
  local status
  echo "[stage2-shadow] direct ROS shadow for ${duration}s (hardware output disabled)"
  set +e
  "${compose[@]}" run --rm --no-deps -T policy-runtime \
    timeout "${duration}" \
    ros2 run a2_piper_stage2_direct a2_piper_stage2_direct --ros-args \
      -p bundle_dir:=/policy_bundle \
      -p enable_motion:=false \
      -p live_acknowledged:=false \
    2>&1 | tee "${session_dir}/03_direct_ros_shadow.log"
  status=${PIPESTATUS[0]}
  set -e
  if [[ ${status} -ne 0 && ${status} -ne 124 ]]; then
    return "${status}"
  fi
  echo "[stage2-shadow] expected timeout exit=${status}"
}

case "${mode}" in
  mock)
    if (( $# > 1 )); then
      echo "Usage: $0 [mock|mock-realtime|ros]" >&2
      exit 2
    fi
    run_cli 01_validate validate --bundle /policy_bundle
    run_cli 02_benchmark benchmark --bundle /policy_bundle
    run_cli 03_mock_shadow mock-shadow --bundle /policy_bundle
    ;;
  mock-realtime)
    if (( $# > 1 )); then
      echo "Usage: $0 [mock|mock-realtime|ros]" >&2
      exit 2
    fi
    run_cli 01_validate validate --bundle /policy_bundle
    run_cli 02_benchmark benchmark --bundle /policy_bundle
    run_cli 03_mock_shadow_realtime mock-shadow --bundle /policy_bundle --realtime
    ;;
  ros)
    if (( $# > 2 )); then
      echo "Usage: $0 ros [duration_seconds]" >&2
      exit 2
    fi
    duration="${2:-60}"
    if [[ ! "${duration}" =~ ^[1-9][0-9]*$ ]]; then
      echo "ERROR: ros duration must be a positive integer number of seconds." >&2
      exit 2
    fi
    run_cli 01_validate validate --bundle /policy_bundle
    run_cli 02_site_check site-check --site /site.yaml
    run_direct_shadow "${duration}"
    ;;
  *)
    echo "Usage: $0 [mock|mock-realtime|ros]" >&2
    exit 2
    ;;
esac

echo "[PASS] shadow sequence completed; evidence: ${session_dir}"
