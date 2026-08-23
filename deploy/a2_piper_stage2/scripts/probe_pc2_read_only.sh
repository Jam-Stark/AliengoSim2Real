#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
env_file="${deploy_dir}/docker/.env"

usage() {
  cat <<USAGE
Usage:
  $0 --local [--session NAME]
  $0 --ssh USER@HOST [--session NAME]

Runs only inventory/read commands. --ssh requires existing key-based SSH.
No PiPER SDK path or revision is assumed; if PIPER_SDK_ROOT is explicitly set
on PC2, its current git revision is recorded.
USAGE
}

probe_mode=""
ssh_target=""
session=""
while (( $# > 0 )); do
  case "$1" in
    --local) probe_mode=local; shift ;;
    --ssh) probe_mode=ssh; ssh_target="${2:?--ssh needs USER@HOST}"; shift 2 ;;
    --session) session="${2:?--session needs a value}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
if [[ -z "${probe_mode}" ]]; then
  echo "ERROR: choose exactly one of --local or --ssh USER@HOST." >&2
  usage >&2
  exit 2
fi

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: missing ${env_file}; run configure_policy_host.sh first." >&2
  exit 1
fi
set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a
: "${EVIDENCE_DIR:?Missing EVIDENCE_DIR in docker/.env}"

session="${session:-$(date +%Y%m%d_%H%M%S)_pc2}"
session_dir="${EVIDENCE_DIR}/${session}"
mkdir -p "${session_dir}"
log_file="${session_dir}/pc2_read_only.log"

read -r -d '' pc2_probe <<'PC2_PROBE' || true
set -eo pipefail
echo "probe=pc2_read_only"
echo "timestamp=$(date --iso-8601=seconds)"
echo "hostname=$(hostname)"
echo "user=$(id -un)"
uname -a
if [ -r /etc/os-release ]; then sed -n '1,40p' /etc/os-release; fi
echo
ip -brief link show
ip -4 addr show
ip route show
echo
if command -v lsusb >/dev/null 2>&1; then lsusb; else echo "lsusb=UNAVAILABLE"; fi
echo
if command -v docker >/dev/null 2>&1; then
  docker --version
  docker ps --format 'container={{.Names}} image={{.Image}} status={{.Status}}'
else
  echo "docker=UNAVAILABLE"
fi
echo
echo "PIPER_SDK_ROOT=${PIPER_SDK_ROOT:-UNSET}"
if [ -n "${PIPER_SDK_ROOT:-}" ]; then
  test -d "${PIPER_SDK_ROOT}"
  git -C "${PIPER_SDK_ROOT}" status --short --branch
  git -C "${PIPER_SDK_ROOT}" remote get-url origin 2>/dev/null || true
fi
echo
if command -v ros2 >/dev/null 2>&1; then
  echo "ROS_DISTRO=${ROS_DISTRO:-UNSET} ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-UNSET}"
  timeout 15 ros2 topic list -t
  timeout 10 ros2 service list -t
else
  echo "ros2=UNAVAILABLE_IN_THIS_SHELL"
fi
echo "[PASS] PC2 read-only probe completed"
PC2_PROBE

if [[ "${probe_mode}" == "local" ]]; then
  bash -lc "${pc2_probe}" 2>&1 | tee "${log_file}"
else
  ssh -o BatchMode=yes "${ssh_target}" bash -s <<<"${pc2_probe}" 2>&1 | tee "${log_file}"
fi

echo "evidence=${log_file}"
