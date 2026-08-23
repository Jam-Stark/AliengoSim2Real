#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
docker_dir="${deploy_dir}/docker"
env_file="${docker_dir}/.env"

usage() {
  cat <<USAGE
Usage: $0 [--connected]

Default checks Docker, paths, and the CPU Compose configuration without
requiring the robot Ethernet link. --connected additionally requires the
configured interface to carry the exact POLICY_HOST_IPV4 address.
USAGE
}

connected=0
case "${1:-}" in
  "") ;;
  --connected) connected=1 ;;
  -h|--help) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  usage >&2
  exit 2
fi

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: missing ${env_file}." >&2
  echo "Run ${script_dir}/configure_policy_host.sh --help first." >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a

command -v docker >/dev/null
docker info >/dev/null
docker compose version >/dev/null
: "${POLICY_BUNDLE_DIR:?Missing POLICY_BUNDLE_DIR in docker/.env}"
: "${SITE_CONFIG_FILE:?Missing SITE_CONFIG_FILE in docker/.env}"
: "${EVIDENCE_DIR:?Missing EVIDENCE_DIR in docker/.env}"
: "${A2_NET_IFACE:?Missing A2_NET_IFACE in docker/.env}"
: "${POLICY_HOST_IPV4:?Missing POLICY_HOST_IPV4 in docker/.env}"
: "${ROS_DOMAIN_ID:?Missing ROS_DOMAIN_ID in docker/.env}"
[[ -f "${POLICY_BUNDLE_DIR}/policy_manifest.yaml" ]]
[[ -f "${POLICY_BUNDLE_DIR}/dog_actor.pt" ]]
[[ -f "${POLICY_BUNDLE_DIR}/arm_actor.pt" ]]
[[ -f "${SITE_CONFIG_FILE}" ]]
[[ -d "${EVIDENCE_DIR}" && -w "${EVIDENCE_DIR}" ]]

docker compose \
  --env-file "${env_file}" \
  -f "${docker_dir}/compose.yaml" \
  config --quiet

if (( connected == 1 )); then
  command -v ip >/dev/null
  ip link show dev "${A2_NET_IFACE}" >/dev/null
  ip -o -4 addr show dev "${A2_NET_IFACE}" \
    | awk '{print $4}' \
    | grep -Fx "${POLICY_HOST_IPV4}" >/dev/null
  echo "[PASS] connected policy host: ${A2_NET_IFACE} has ${POLICY_HOST_IPV4}; Compose is valid."
else
  echo "[PASS] policy host prerequisites and CPU Compose configuration are valid."
fi
