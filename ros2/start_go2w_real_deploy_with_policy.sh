#!/bin/sh
[ -n "${BASH_VERSION:-}" ] || exec bash "$0" "$@"
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <policy_key> <policy_path> [extra go2w_real_deploy args...]" >&2
  echo "Valid policy_key values: motion_mlp | vtm | vtm_lstm_sru | vtm_gru_sru" >&2
  exit 1
fi

POLICY_KEY="$1"
POLICY_PATH="$2"
shift 2

case "${POLICY_KEY}" in
  motion_mlp|vtm|vtm_lstm_sru|vtm_gru_sru)
    ;;
  *)
    echo "[go2w_real_deploy] Invalid policy key: ${POLICY_KEY}" >&2
    echo "[go2w_real_deploy] Valid keys: motion_mlp | vtm | vtm_lstm_sru | vtm_gru_sru" >&2
    exit 1
    ;;
esac

if [[ ! -e "${POLICY_PATH}" ]]; then
  echo "[go2w_real_deploy] Policy path does not exist: ${POLICY_PATH}" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${SCRIPT_DIR}/start_go2w_real_deploy_wireless_only.sh" \
  "${POLICY_KEY}=${POLICY_PATH}" \
  "$@"
