#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
env_file="${deploy_dir}/docker/.env"

usage() {
  cat <<USAGE
Usage:
  $0 --iface IFACE --host-ip IPV4/CIDR --domain-id ID [options]

Required explicit values:
  --iface IFACE       Robot Ethernet interface (observed host: enp131s0)
  --host-ip CIDR      Address on that interface (observed host: 192.168.123.222/24)
  --domain-id ID      ROS_DOMAIN_ID shared by policy host, A2, and PC2 (example: 0)

Options:
  --bundle DIR        Extracted bundle; default: ${deploy_dir}/policy_bundle
  --site FILE         Site YAML; default: ${deploy_dir}/config/site.mock.yaml
  --evidence DIR      Writable output directory; default: ${deploy_dir}/evidence
  --apply-network     Run sudo ip link/ip addr replace for the stated IFACE/CIDR
  --force             Replace an existing docker/.env
  -h, --help          Show this help

Without --apply-network this command never changes host networking.
USAGE
}

iface=""
host_ip=""
domain_id=""
bundle_dir="${deploy_dir}/policy_bundle"
site_file="${deploy_dir}/config/site.mock.yaml"
evidence_dir="${deploy_dir}/evidence"
apply_network=0
force=0

while (( $# > 0 )); do
  case "$1" in
    --iface) iface="${2:?--iface needs a value}"; shift 2 ;;
    --host-ip) host_ip="${2:?--host-ip needs a value}"; shift 2 ;;
    --domain-id) domain_id="${2:?--domain-id needs a value}"; shift 2 ;;
    --bundle) bundle_dir="${2:?--bundle needs a value}"; shift 2 ;;
    --site) site_file="${2:?--site needs a value}"; shift 2 ;;
    --evidence) evidence_dir="${2:?--evidence needs a value}"; shift 2 ;;
    --apply-network) apply_network=1; shift ;;
    --force) force=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "${iface}" || -z "${host_ip}" || -z "${domain_id}" ]]; then
  echo "ERROR: --iface, --host-ip, and --domain-id must all be stated explicitly." >&2
  usage >&2
  exit 2
fi
if [[ ! "${domain_id}" =~ ^[0-9]+$ ]] || (( domain_id > 232 )); then
  echo "ERROR: --domain-id must be an integer from 0 through 232." >&2
  exit 2
fi
python3 - "${host_ip}" <<'PY'
import ipaddress
import sys

value = ipaddress.ip_interface(sys.argv[1])
if value.version != 4:
    raise SystemExit("--host-ip must be IPv4/CIDR")
PY

if [[ ! -d "${bundle_dir}" || ! -f "${bundle_dir}/policy_manifest.yaml" ]]; then
  echo "ERROR: bundle directory must contain policy_manifest.yaml: ${bundle_dir}" >&2
  exit 1
fi
if [[ ! -f "${site_file}" ]]; then
  echo "ERROR: site configuration not found: ${site_file}" >&2
  exit 1
fi
if [[ -e "${env_file}" && ${force} -ne 1 ]]; then
  echo "ERROR: ${env_file} already exists; review it, then rerun with --force." >&2
  exit 1
fi

bundle_dir="$(cd "${bundle_dir}" && pwd)"
site_file="$(cd "$(dirname "${site_file}")" && pwd)/$(basename "${site_file}")"
mkdir -p "${evidence_dir}"
evidence_dir="$(cd "${evidence_dir}" && pwd)"

if (( apply_network == 1 )); then
  command -v ip >/dev/null
  ip link show dev "${iface}" >/dev/null
  sudo -v
  sudo ip link set dev "${iface}" up
  sudo ip addr replace "${host_ip}" dev "${iface}"
  ip -o -4 addr show dev "${iface}" | awk '{print $4}' | grep -Fx "${host_ip}" >/dev/null
  echo "[PASS] host network applied: ${iface} ${host_ip}"
else
  echo "[INFO] host network unchanged (no --apply-network)."
fi

tmp_env="$(mktemp "${deploy_dir}/docker/.env.tmp.XXXXXX")"
trap 'rm -f "${tmp_env}"' EXIT
{
  echo "A2_BASE_IMAGE=a2-humble-deploy:2026-06-05"
  echo "A2_PIPER_STAGE2_IMAGE=a2-piper-stage2:humble-torch2.7.0-cpu"
  echo "A2_DOCKER_PLATFORM=linux/amd64"
  echo "TORCH_VERSION=2.7.0"
  echo "TORCH_INDEX_URL=https://download.pytorch.org/whl/cpu"
  echo "LIBTORCH_URL=https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.7.0%2Bcpu.zip"
  echo "NUMPY_VERSION=1.26.0"
  echo "PYYAML_VERSION=6.0.2"
  echo "POLICY_BUNDLE_DIR=${bundle_dir}"
  echo "SITE_CONFIG_FILE=${site_file}"
  echo "EVIDENCE_DIR=${evidence_dir}"
  echo "A2_NET_IFACE=${iface}"
  echo "POLICY_HOST_IPV4=${host_ip}"
  echo "ROS_DOMAIN_ID=${domain_id}"
  echo "A2_LOWSTATE_TOPIC=/lowstate"
  echo "A2_LOWCMD_TOPIC=/lowcmd"
} > "${tmp_env}"
mv "${tmp_env}" "${env_file}"
trap - EXIT

echo "[PASS] wrote ${env_file}"
echo "       iface=${iface} host_ip=${host_ip} ROS_DOMAIN_ID=${domain_id}"
echo "Next: ${script_dir}/check_policy_host.sh"
