#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
docker_dir="${deploy_dir}/docker"
env_file="${docker_dir}/.env"

usage() {
  cat <<USAGE
Usage: $0 [--a2-only] [--session NAME]

Runs the existing ros2/A2/scripts/a2_real_robot_test.sh connected-preflight
inside the Stage2 container, then records the ROS graph. Default also requires
the known PiPER semantic topics/services. It never publishes a command.
USAGE
}

a2_only=0
session=""
while (( $# > 0 )); do
  case "$1" in
    --a2-only) a2_only=1; shift ;;
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

session="${session:-$(date +%Y%m%d_%H%M%S)_ros_graph}"
session_dir="${EVIDENCE_DIR}/${session}"
mkdir -p "${session_dir}"

compose=(
  docker compose
  --env-file "${env_file}"
  -f "${docker_dir}/compose.yaml"
)

"${compose[@]}" run --rm --no-deps -T \
  -e "A2_TEST_LOG_DIR=/evidence/${session}/a2_existing" \
  -e "STAGE2_EVIDENCE_SESSION=/evidence/${session}" \
  -e "STAGE2_A2_ONLY=${a2_only}" \
  policy-runtime bash -lc '
    set -euo pipefail
    mkdir -p "${STAGE2_EVIDENCE_SESSION}"
    /opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh \
      connected-preflight "${A2_NET_IFACE}" \
      2>&1 | tee "${STAGE2_EVIDENCE_SESSION}/a2_connected_preflight.log"

    {
      echo "probe=ros_graph_read_only"
      echo "timestamp=$(date --iso-8601=seconds)"
      echo "iface=${A2_NET_IFACE} domain=${ROS_DOMAIN_ID}"
      timeout 15 ros2 node list
      timeout 15 ros2 topic list -t
      timeout 15 ros2 service list -t

      if [[ "${STAGE2_A2_ONLY}" == "0" ]]; then
        expect_topic_type() {
          local topic="$1"
          local expected="$2"
          local actual
          actual="$(timeout 10 ros2 topic type "${topic}")"
          [[ "${actual}" == "${expected}" ]] || {
            echo "ERROR: ${topic} type=${actual:-MISSING}; expected ${expected}" >&2
            return 1
          }
          echo "PASS topic ${topic} type=${actual}"
        }
        expect_service_type() {
          local service="$1"
          local expected="$2"
          local actual
          actual="$(timeout 10 ros2 service type "${service}")"
          [[ "${actual}" == "${expected}" ]] || {
            echo "ERROR: ${service} type=${actual:-MISSING}; expected ${expected}" >&2
            return 1
          }
          echo "PASS service ${service} type=${actual}"
        }

        expect_topic_type /piper/joint_states sensor_msgs/msg/JointState
        expect_topic_type /piper/joint_command trajectory_msgs/msg/JointTrajectory
        expect_topic_type /piper/diagnostics diagnostic_msgs/msg/DiagnosticArray
        expect_service_type /piper/resume std_srvs/srv/Trigger
        expect_service_type /piper/enable std_srvs/srv/Trigger
        expect_service_type /piper/stop std_srvs/srv/Trigger
        expect_service_type /piper/disable std_srvs/srv/Trigger
      fi
      echo "[PASS] ROS graph read-only probe completed"
    } 2>&1 | tee "${STAGE2_EVIDENCE_SESSION}/ros_graph_read_only.log"
  '

echo "evidence=${session_dir}"
