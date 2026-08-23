#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
docker_dir="${deploy_dir}/docker"
env_file="${docker_dir}/.env"

usage() {
  echo "Usage: $0 [--session NAME]"
}

session=""
while (( $# > 0 )); do
  case "$1" in
    --session) session="${2:?--session needs a value}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: missing ${env_file}; run configure_policy_host.sh first." >&2
  exit 1
fi
set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a
: "${EVIDENCE_DIR:?Missing EVIDENCE_DIR in docker/.env}"
: "${A2_NET_IFACE:?Missing A2_NET_IFACE in docker/.env}"
: "${POLICY_HOST_IPV4:?Missing POLICY_HOST_IPV4 in docker/.env}"

session="${session:-$(date +%Y%m%d_%H%M%S)_policy_host}"
session_dir="${EVIDENCE_DIR}/${session}"
mkdir -p "${session_dir}"
log_file="${session_dir}/policy_host_read_only.log"

{
  echo "probe=policy_host_read_only"
  echo "timestamp=$(date --iso-8601=seconds)"
  echo "configured_iface=${A2_NET_IFACE}"
  echo "configured_ipv4=${POLICY_HOST_IPV4}"
  echo "configured_domain=${ROS_DOMAIN_ID}"
  echo
  uname -a
  if [[ -r /etc/os-release ]]; then
    sed -n '1,40p' /etc/os-release
  fi
  echo
  docker --version
  docker compose version
  docker info --format 'server={{.ServerVersion}} driver={{.Driver}} os={{.OperatingSystem}} arch={{.Architecture}}'
  echo
  ip -brief link show dev "${A2_NET_IFACE}"
  ip -4 addr show dev "${A2_NET_IFACE}"
  ip route show
  echo
  docker compose --env-file "${env_file}" -f "${docker_dir}/compose.yaml" config --quiet
  echo "compose_cpu=valid"
  if docker image inspect "${A2_PIPER_STAGE2_IMAGE}" >/dev/null 2>&1; then
    docker image inspect "${A2_PIPER_STAGE2_IMAGE}" \
      --format 'stage2_image={{.RepoTags}} created={{.Created}} architecture={{.Architecture}}'
  else
    echo "stage2_image=NOT_BUILT"
  fi
  echo
  find "${POLICY_BUNDLE_DIR}" -maxdepth 2 -type f -printf '%P\n' | sort
  echo "[PASS] policy-host read-only probe completed"
} 2>&1 | tee "${log_file}"

echo "evidence=${log_file}"
