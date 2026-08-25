#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deploy_dir="$(cd "${script_dir}/.." && pwd)"
sessions_root="${deploy_dir}/.stage2_sessions"
current_file="${sessions_root}/CURRENT"
docker_dir="${deploy_dir}/docker"
env_file="${docker_dir}/.env"

site_file="${STAGE2_SITE_FILE:-${deploy_dir}/config/site.yaml}"
params_file="${STAGE2_PARAMS_FILE:-${deploy_dir}/config/stage2_direct.params.yaml}"
a2_test_script="/opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh"

session_id=""
operator=""
component=""
gate=""
reason=""
iface=""
domain_id="0"
a2_ip="192.168.123.161"
pc2_ip="192.168.123.162"
duration=""
live_requested=0
scenario="process-stop"
evidence_path=""
arm_radius=""
arm_pitch=""
arm_yaw=""

usage() {
  cat <<'USAGE'
Usage:
  stage2_gate.sh init --operator NAME [--session ID]
  stage2_gate.sh status [--session ID]
  stage2_gate.sh next [--session ID]
  stage2_gate.sh approve --gate GATE --operator NAME [--session ID]
  stage2_gate.sh offline [--session ID]
  stage2_gate.sh network --iface NIC [--domain-id ID] [--a2-ip IP] [--pc2-ip IP]
  stage2_gate.sh ros-readonly [--duration SEC]
  STAGE2_ALLOW_A2_BASELINE=1 stage2_gate.sh a2-baseline --iface NIC --live --operator NAME
  stage2_gate.sh audit-a2-baseline --iface NIC --evidence PATH
  STAGE2_ALLOW_PIPER_BASELINE=1 stage2_gate.sh piper-baseline --live --operator NAME
  stage2_gate.sh dry-run [--duration SEC]
  stage2_gate.sh joint-observe [--duration SEC]
  stage2_gate.sh fault [--scenario process-stop] [--duration SEC]
  stage2_gate.sh shadow [--duration SEC]
  stage2_gate.sh live-preflight --component dog_only|arm_only|both --operator NAME
  STAGE2_ALLOW_LIVE=1 stage2_gate.sh live --component dog_only|arm_only|both --live --operator NAME
  stage2_gate.sh arm-goal --radius M --pitch RAD --yaw RAD --operator NAME
  stage2_gate.sh trajectory --operator NAME
  stage2_gate.sh stop --operator NAME [--reason TEXT]
  STAGE2_ALLOW_A2_RESTORE=1 stage2_gate.sh restore-a2 --iface NIC --operator NAME

Receipts are written below deploy/a2_piper_stage2/.stage2_sessions/<id>/.
The script never edits site.yaml, output_enabled, ROS params, or PiPER resume state.
USAGE
}

die() {
  echo "ERROR: $*" >&2
  exit 2
}

timestamp() {
  date +%Y%m%d_%H%M%S
}

validate_token() {
  local name="$1"
  local value="$2"
  [[ "$value" =~ ^[A-Za-z0-9._-]+$ ]] ||
    die "${name} may contain only letters, digits, '.', '_' and '-': ${value}"
}

parse_options() {
  while (( $# > 0 )); do
    case "$1" in
      --session)
        (( $# >= 2 )) || die "--session requires a value"
        session_id="$2"
        shift 2
        ;;
      --operator)
        (( $# >= 2 )) || die "--operator requires a value"
        operator="$2"
        shift 2
        ;;
      --component)
        (( $# >= 2 )) || die "--component requires a value"
        component="$2"
        shift 2
        ;;
      --gate)
        (( $# >= 2 )) || die "--gate requires a value"
        gate="$2"
        shift 2
        ;;
      --reason)
        (( $# >= 2 )) || die "--reason requires a value"
        reason="$2"
        shift 2
        ;;
      --iface)
        (( $# >= 2 )) || die "--iface requires a value"
        iface="$2"
        shift 2
        ;;
      --domain-id)
        (( $# >= 2 )) || die "--domain-id requires a value"
        domain_id="$2"
        shift 2
        ;;
      --a2-ip)
        (( $# >= 2 )) || die "--a2-ip requires a value"
        a2_ip="$2"
        shift 2
        ;;
      --pc2-ip)
        (( $# >= 2 )) || die "--pc2-ip requires a value"
        pc2_ip="$2"
        shift 2
        ;;
      --duration)
        (( $# >= 2 )) || die "--duration requires a value"
        duration="$2"
        shift 2
        ;;
      --scenario)
        (( $# >= 2 )) || die "--scenario requires a value"
        scenario="$2"
        shift 2
        ;;
      --evidence)
        (( $# >= 2 )) || die "--evidence requires a value"
        evidence_path="$2"
        shift 2
        ;;
      --radius)
        (( $# >= 2 )) || die "--radius requires a value"
        arm_radius="$2"
        shift 2
        ;;
      --pitch)
        (( $# >= 2 )) || die "--pitch requires a value"
        arm_pitch="$2"
        shift 2
        ;;
      --yaw)
        (( $# >= 2 )) || die "--yaw requires a value"
        arm_yaw="$2"
        shift 2
        ;;
      --live)
        live_requested=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "unknown option: $1"
        ;;
    esac
  done
}

load_session() {
  mkdir -p "$sessions_root"
  if [[ -z "$session_id" ]]; then
    [[ -f "$current_file" ]] || die "no current session; run '$0 init --operator NAME' first"
    session_id="$(<"$current_file")"
  fi
  validate_token session "$session_id"
  session_dir="${sessions_root}/${session_id}"
  [[ -d "$session_dir" ]] || die "session does not exist: ${session_id}"
}

require_operator() {
  [[ -n "$operator" ]] || die "--operator NAME is required"
  [[ "$operator" != *$'\n'* ]] || die "operator must be one line"
}

validate_component() {
  case "$component" in
    dog_only|arm_only|both) ;;
    *) die "--component must be dog_only, arm_only, or both" ;;
  esac
}

validate_duration() {
  [[ "$duration" =~ ^[1-9][0-9]*$ ]] || die "duration must be a positive integer number of seconds"
}

step_pass_file() {
  printf '%s/results/%s.pass\n' "$session_dir" "$1"
}

approval_file() {
  printf '%s/approvals/%s.receipt\n' "$session_dir" "$1"
}

require_pass() {
  local step="$1"
  [[ -f "$(step_pass_file "$step")" ]] ||
    die "missing successful ${step} receipt; run '$0 ${step}' first"
}

require_approval() {
  local approval="$1"
  [[ -f "$(approval_file "$approval")" ]] ||
    die "missing human approval; run '$0 approve --gate ${approval} --operator NAME'"
}

new_evidence_dir() {
  local step="$1"
  local path="${session_dir}/evidence/${step}/$(timestamp)_$$"
  mkdir -p "$path"
  printf '%s\n' "$path"
}

write_command() {
  local path="$1"
  shift
  {
    printf 'cwd=%q\n' "$PWD"
    printf 'command='
    printf '%q ' "$@"
    printf '\n'
  } > "${path}/command.txt"
}

mark_pass() {
  local step="$1"
  local evidence="$2"
  local pass_file
  pass_file="$(step_pass_file "$step")"
  mkdir -p "$(dirname "$pass_file")"
  {
    echo "result=PASS"
    echo "step=${step}"
    echo "time=$(date --iso-8601=seconds)"
    echo "evidence=${evidence}"
  } > "$pass_file"
  echo "PASS: ${step}"
  echo "evidence: ${evidence}"
}

record_failure() {
  local step="$1"
  local evidence="$2"
  local status="$3"
  {
    echo "result=FAIL"
    echo "step=${step}"
    echo "exit=${status}"
    echo "time=$(date --iso-8601=seconds)"
  } > "${evidence}/result.receipt"
  echo "FAIL: ${step}; stop here. Evidence: ${evidence}" >&2
  return "$status"
}

run_logged() {
  local evidence="$1"
  shift
  write_command "$evidence" "$@"
  "$@" 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  return "$status"
}

load_compose() {
  [[ -f "$env_file" ]] || die "missing ${env_file}; run configure_policy_host.sh first"
  command -v docker >/dev/null || die "docker is not installed"
  compose=(
    docker compose
    --env-file "$env_file"
    -f "${docker_dir}/compose.yaml"
  )
}

require_direct_files() {
  [[ -f "$params_file" ]] || die "direct-node params not found: ${params_file}"
  grep -Eq '^[[:space:]]*enable_motion:[[:space:]]*false[[:space:]]*$' "$params_file" ||
    die "params file must keep enable_motion: false"
  grep -Eq '^[[:space:]]*live_acknowledged:[[:space:]]*false[[:space:]]*$' "$params_file" ||
    die "params file must keep live_acknowledged: false"
  grep -Eq '^[[:space:]]*validate_live_site_only:[[:space:]]*false[[:space:]]*$' "$params_file" ||
    die "params file must keep validate_live_site_only: false"
}

require_site_live_ready() {
  require_direct_files
  [[ -f "$site_file" ]] || die "site config not found: ${site_file}; copy config/site.template.yaml and fill site-specific fields"
  [[ -f "$env_file" ]] || die "missing ${env_file}; run configure_policy_host.sh first"
  local configured_site site_abs configured_site_abs
  configured_site="$(awk -F= '$1 == "SITE_CONFIG_FILE" {print substr($0, index($0, "=") + 1)}' "$env_file")"
  [[ -n "$configured_site" && -f "$configured_site" ]] ||
    die "docker/.env SITE_CONFIG_FILE is missing or not a file: ${configured_site:-MISSING}"
  site_abs="$(cd "$(dirname "$site_file")" && pwd)/$(basename "$site_file")"
  configured_site_abs="$(cd "$(dirname "$configured_site")" && pwd)/$(basename "$configured_site")"
  [[ "$site_abs" == "$configured_site_abs" ]] ||
    die "live site ${site_abs} differs from docker/.env SITE_CONFIG_FILE=${configured_site_abs}"
  if grep -nF 'TO_VERIFY' "$site_file"; then
    die "site config still contains TO_VERIFY; fill it from site evidence before live"
  fi
  grep -Eq '^[[:space:]]*mode:[[:space:]]*a2_direct_lowcmd[[:space:]]*$' "$site_file" ||
    die "site config topology.mode must be a2_direct_lowcmd"
  grep -Eq '^[[:space:]]*output_enabled:[[:space:]]*true[[:space:]]*$' "$site_file" ||
    die "site safety.output_enabled is not true; an operator must review and edit site.yaml manually"
}

cmd_init() {
  require_operator
  mkdir -p "$sessions_root"
  if [[ -z "$session_id" ]]; then
    session_id="$(date +%Y%m%d_%H%M%S)"
  fi
  validate_token session "$session_id"
  session_dir="${sessions_root}/${session_id}"
  [[ ! -e "$session_dir" ]] || die "session already exists: ${session_id}"
  mkdir -p "$session_dir/evidence" "$session_dir/results" "$session_dir/approvals"
  {
    echo "session=${session_id}"
    echo "created_at=$(date --iso-8601=seconds)"
    echo "created_by=${operator}"
    echo "deploy_dir=${deploy_dir}"
  } > "${session_dir}/session.receipt"
  printf '%s\n' "$session_id" > "$current_file"
  echo "PASS: initialized Stage2 session ${session_id}"
  echo "receipts: ${session_dir}"
  echo "Next: $0 offline"
}

show_status() {
  load_session
  echo "session=${session_id}"
  echo "receipts=${session_dir}"
  echo
  local item state
  for item in offline network ros-readonly a2-baseline piper-baseline dry-run joint-observe fault shadow; do
    state=PENDING
    [[ -f "$(step_pass_file "$item")" ]] && state=PASS
    [[ -f "$(approval_file "$item")" ]] && state="${state}+APPROVED"
    printf '%-18s %s\n' "$item" "$state"
  done
  for item in dog_only arm_only both; do
    state=PENDING
    [[ -f "$(step_pass_file "live-preflight-${item}")" ]] && state=PREFLIGHT_PASS
    [[ -f "$(approval_file "live-${item}")" ]] && state="${state}+APPROVED"
    printf '%-18s %s\n' "live-${item}" "$state"
  done
  [[ -f "$(approval_file physical)" ]] && echo "physical           APPROVED" || echo "physical           PENDING"
  [[ -f "$(approval_file joint-validation)" ]] && echo "joint-validation   APPROVED" || echo "joint-validation   PENDING"
}

show_next() {
  load_session
  if [[ ! -f "$(step_pass_file offline)" ]]; then echo "$0 offline"; return; fi
  if [[ ! -f "$(approval_file offline)" ]]; then echo "$0 approve --gate offline --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file network)" ]]; then echo "$0 network --iface <A2有线网卡>"; return; fi
  if [[ ! -f "$(approval_file network)" ]]; then echo "$0 approve --gate network --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file ros-readonly)" ]]; then echo "$0 ros-readonly"; return; fi
  if [[ ! -f "$(approval_file physical)" ]]; then echo "$0 approve --gate physical --operator NAME"; return; fi
  if [[ ! -f "$(approval_file ros-readonly)" ]]; then echo "$0 approve --gate ros-readonly --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file a2-baseline)" ]]; then echo "STAGE2_ALLOW_A2_BASELINE=1 $0 a2-baseline --iface <A2有线网卡> --live --operator NAME"; return; fi
  if [[ ! -f "$(approval_file a2-baseline)" ]]; then echo "$0 approve --gate a2-baseline --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file piper-baseline)" ]]; then echo "STAGE2_ALLOW_PIPER_BASELINE=1 $0 piper-baseline --live --operator NAME"; return; fi
  if [[ ! -f "$(approval_file piper-baseline)" ]]; then echo "$0 approve --gate piper-baseline --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file dry-run)" ]]; then echo "$0 dry-run"; return; fi
  if [[ ! -f "$(approval_file dry-run)" ]]; then echo "$0 approve --gate dry-run --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file joint-observe)" ]]; then echo "$0 joint-observe --duration 600"; return; fi
  if [[ ! -f "$(step_pass_file fault)" ]]; then echo "$0 fault --scenario process-stop"; return; fi
  if [[ ! -f "$(approval_file fault)" ]]; then echo "$0 approve --gate fault --operator NAME"; return; fi
  if [[ ! -f "$(step_pass_file shadow)" ]]; then echo "$0 shadow --duration 600"; return; fi
  if [[ ! -f "$(step_pass_file live-preflight-both)" ]]; then echo "$0 live-preflight --component both --operator NAME"; return; fi
  if [[ ! -f "$(approval_file live-both)" ]]; then echo "$0 approve --gate live-both --operator NAME"; return; fi
  echo "STAGE2_ALLOW_LIVE=1 $0 live --component both --live --operator NAME"
}

cmd_approve() {
  load_session
  require_operator
  [[ -n "$gate" ]] || die "approve requires --gate"
  validate_token gate "$gate"
  case "$gate" in
    physical) ;;
    offline|network|ros-readonly|a2-baseline|piper-baseline|dry-run|fault|shadow)
      require_pass "$gate"
      ;;
    joint-validation)
      require_pass joint-observe
      ;;
    live-dog_only|live-arm_only|live-both)
      require_pass shadow
      ;;
    *) die "unsupported approval gate: ${gate}" ;;
  esac
  local receipt joint_evidence=""
  receipt="$(approval_file "$gate")"
  [[ ! -e "$receipt" ]] || die "approval already exists: ${receipt}"
  if [[ "$gate" == "joint-validation" ]]; then
    joint_evidence="$(awk -F= '$1 == "evidence" {print substr($0, index($0, "=") + 1)}' "$(step_pass_file joint-observe)")"
    [[ -f "${joint_evidence}/joint_validation_table.tsv" ]] ||
      die "joint validation table is missing from ${joint_evidence}"
    awk -F '\t' '
      NR == 1 {
        if (NF != 11 || $1 != "component" || $2 != "policy_joint") exit 1
        next
      }
      {
        if (NF != 11) exit 1
        for (column = 4; column <= 10; ++column) {
          if ($column == "") exit 1
        }
      }
      END { if (NR != 19) exit 1 }
    ' "${joint_evidence}/joint_validation_table.tsv" ||
      die "joint validation table must contain 18 rows with direction, unit, zero, lower, upper, stop result, and reviewer filled"
  fi
  {
    echo "decision=APPROVED"
    echo "gate=${gate}"
    echo "operator=${operator}"
    echo "time=$(date --iso-8601=seconds)"
    echo "site_file=${site_file}"
    echo "params_file=${params_file}"
    if [[ "$gate" == "joint-validation" ]]; then
      echo "reviewed_table=${joint_evidence}/joint_validation_table.tsv"
      echo "human_onsite_joint_table_reviewed=true"
      echo "reviewed_by=${operator}"
      echo "one_joint_at_a_time_reviewed=true"
      echo "mapping_reviewed=true"
      echo "direction_reviewed=true"
      echo "unit_reviewed=true"
      echo "zero_reviewed=true"
      echo "limits_reviewed=true"
      echo "stop_result_reviewed=true"
    fi
  } > "$receipt"
  echo "PASS: human approval recorded for ${gate}"
  echo "receipt: ${receipt}"
}

cmd_offline() {
  load_session
  local evidence
  evidence="$(new_evidence_dir offline)"
  write_command "$evidence" "${script_dir}/check_policy_host.sh" "&&" "${script_dir}/build_container.sh" "&&" "${script_dir}/run_shadow.sh" mock
  set +e
  {
    "${script_dir}/check_policy_host.sh" &&
    "${script_dir}/build_container.sh" &&
    "${script_dir}/run_shadow.sh" mock
  } 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure offline "$evidence" "$status"; exit "$status"; }
  grep -q '"status": "pass"' "${evidence}/stdout.log" ||
    die "offline commands exited 0 but expected parity/mock pass text was not found; inspect ${evidence}"
  mark_pass offline "$evidence"
}

cmd_network() {
  load_session
  require_pass offline
  require_approval offline
  [[ -f "$env_file" ]] || die "missing ${env_file}; run configure_policy_host.sh first"
  [[ -n "$iface" ]] || die "network requires --iface NIC"
  validate_token iface "$iface"
  [[ "$domain_id" =~ ^[0-9]+$ ]] && (( domain_id <= 232 )) ||
    die "ROS domain ID must be an integer from 0 through 232"
  local configured_iface configured_domain configured_host_ip
  configured_iface="$(awk -F= '$1 == "A2_NET_IFACE" {print substr($0, index($0, "=") + 1)}' "$env_file")"
  configured_domain="$(awk -F= '$1 == "ROS_DOMAIN_ID" {print substr($0, index($0, "=") + 1)}' "$env_file")"
  configured_host_ip="$(awk -F= '$1 == "POLICY_HOST_IPV4" {print substr($0, index($0, "=") + 1)}' "$env_file")"
  [[ -n "$configured_iface" && -n "$configured_domain" && -n "$configured_host_ip" ]] ||
    die "docker/.env is missing A2_NET_IFACE, ROS_DOMAIN_ID, or POLICY_HOST_IPV4"
  [[ "$configured_iface" == "$iface" ]] ||
    die "CLI --iface=${iface} differs from docker/.env A2_NET_IFACE=${configured_iface}"
  [[ "$configured_domain" == "$domain_id" ]] ||
    die "CLI --domain-id=${domain_id} differs from docker/.env ROS_DOMAIN_ID=${configured_domain}"
  local evidence
  evidence="$(new_evidence_dir network)"
  export ROS_DOMAIN_ID="$domain_id"
  write_command "$evidence" ip link show dev "$iface"
  set +e
  (
    set -e
    echo "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"
    echo "docker_env_iface=${configured_iface}"
    echo "docker_env_domain=${configured_domain}"
    echo "docker_env_host_ip=${configured_host_ip}"
    ip link show dev "$iface"
    ip -4 addr show dev "$iface"
    ip -o -4 addr show dev "$iface" | awk '{print $4}' | grep -Fx "$configured_host_ip"
    ip route get "$a2_ip"
    ip route get "$pc2_ip"
    ping -c 3 -W 2 "$a2_ip"
    ping -c 3 -W 2 "$pc2_ip"
  ) 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure network "$evidence" "$status"; exit "$status"; }
  {
    echo "interface=${iface}"
    echo "ros_domain_id=${domain_id}"
    echo "a2_ip=${a2_ip}"
    echo "pc2_ip=${pc2_ip}"
    echo "docker_env_host_ip=${configured_host_ip}"
  } > "${evidence}/network.receipt"
  mark_pass network "$evidence"
}

cmd_ros_readonly() {
  load_session
  require_pass network
  require_approval network
  duration="${duration:-10}"
  validate_duration
  load_compose
  local evidence
  evidence="$(new_evidence_dir ros-readonly)"
  write_command "$evidence" docker compose run policy-runtime ros2-readonly
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_DURATION=${duration}" \
    -v "${evidence}:/gate_evidence" \
    policy-runtime bash -lc '
      set -euo pipefail
      echo "lowstate_type=$(ros2 topic type /lowstate)"
      echo "lowcmd_type=$(ros2 topic type /lowcmd)"
      echo "piper_state_type=$(ros2 topic type /piper/joint_states)"
      echo "piper_command_type=$(ros2 topic type /piper/joint_command)"
      ros2 topic info -v /lowstate
      ros2 topic info -v /piper/joint_states
      ros2 topic echo --once /lowstate
      ros2 topic echo --once /piper/joint_states
      timeout "${STAGE2_GATE_DURATION}" ros2 topic hz /lowstate || [[ $? -eq 124 ]]
      timeout "${STAGE2_GATE_DURATION}" ros2 topic hz /piper/joint_states || [[ $? -eq 124 ]]
    ' 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure ros-readonly "$evidence" "$status"; exit "$status"; }
  grep -q 'lowstate_type=unitree_hg/msg/LowState' "${evidence}/stdout.log" || die "unexpected /lowstate type"
  grep -q 'piper_state_type=sensor_msgs/msg/JointState' "${evidence}/stdout.log" || die "unexpected PiPER state type"
  for joint in arm_j1 arm_j2 arm_j3 arm_j4 arm_j5 arm_j6; do
    grep -q "$joint" "${evidence}/stdout.log" || die "PiPER state is missing ${joint}"
  done
  mark_pass ros-readonly "$evidence"
}

cmd_a2_baseline() {
  load_session
  require_approval physical
  require_pass ros-readonly
  require_approval ros-readonly
  require_operator
  [[ -n "$iface" ]] || die "a2-baseline requires --iface NIC"
  validate_token iface "$iface"
  (( live_requested == 1 )) || die "A2 standalone motion baseline requires explicit --live"
  [[ "${STAGE2_ALLOW_A2_BASELINE:-}" == "1" ]] || die "set STAGE2_ALLOW_A2_BASELINE=1 in this shell"
  load_compose
  duration="${duration:-120}"
  validate_duration
  local evidence
  evidence="$(new_evidence_dir a2-baseline)"
  write_command "$evidence" docker compose run policy-runtime "$a2_test_script" policy-enable-remote "$duration"
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs" \
    -e "STAGE2_GATE_IFACE=${iface}" \
    -e "STAGE2_GATE_DURATION=${duration}" \
    -e A2_ALLOW_ENABLE_MOTION=1 \
    -e A2_ALLOW_RELEASE_MODE=1 \
    -e A2_ALLOW_SELECT_MODE=1 \
    -v "${evidence}:/gate_evidence" \
    policy-runtime bash -lc '
      set -euo pipefail
      a2=/opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh
      "$a2" connected-preflight "${STAGE2_GATE_IFACE}"
      "$a2" lowstate 10
      "$a2" joints 10
      "$a2" motion-check "${STAGE2_GATE_IFACE}"
      "$a2" motion-release "${STAGE2_GATE_IFACE}"
      "$a2" motion-check "${STAGE2_GATE_IFACE}"
      "$a2" no-lowcmd 5
      "$a2" policy-enable-remote "${STAGE2_GATE_DURATION}"
      "$a2" no-lowcmd 5
      "$a2" motion-restore "${STAGE2_GATE_IFACE}"
    ' 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure a2-baseline "$evidence" "$status"; exit "$status"; }
  {
    echo "operator=${operator}"
    echo "motion_authorization=STAGE2_ALLOW_A2_BASELINE=1 + --live"
    echo "observation=operator must approve stand/hold/stop separately"
  } > "${evidence}/operator.receipt"
  mark_pass a2-baseline "$evidence"
}

cmd_audit_a2_baseline() {
  load_session
  require_approval physical
  require_pass ros-readonly
  require_approval ros-readonly
  [[ -n "$iface" ]] || die "audit-a2-baseline requires --iface NIC"
  validate_token iface "$iface"
  [[ -n "$evidence_path" ]] || die "audit-a2-baseline requires --evidence PATH"
  local evidence_root resolved_evidence audit
  evidence_root="${session_dir}/evidence/a2-baseline"
  resolved_evidence="$(cd "$evidence_path" && pwd)"
  [[ "$resolved_evidence" == "$evidence_root"/* ]] ||
    die "evidence must be an a2-baseline directory in the current session"
  [[ -f "$resolved_evidence/stdout.log" ]] || die "missing stdout.log in evidence"
  [[ -f "$resolved_evidence/result.receipt" ]] || die "missing result.receipt in evidence"
  grep -qx 'result=FAIL' "$resolved_evidence/result.receipt" ||
    die "audit is only for an operator Ctrl+C run recorded as FAIL"
  grep -qx 'exit=255' "$resolved_evidence/result.receipt" ||
    die "expected Docker Compose Ctrl+C exit=255"
  grep -q 'A2 stand-up interpolation complete' "$resolved_evidence/stdout.log" ||
    die "evidence does not contain completed stand-up interpolation"
  grep -q 'A2 policy handover complete: entering PolicyActive' "$resolved_evidence/stdout.log" ||
    die "evidence does not contain completed policy handover"
  grep -q 'A2 controlled down interpolation complete: entering HoldProne' "$resolved_evidence/stdout.log" ||
    die "evidence does not contain completed controlled-down interpolation"
  if docker ps --format '{{.Names}}' | grep -q '^a2-piper-stage2-policy-runtime-run-'; then
    die "an A2/Stage2 policy-runtime container is still running"
  fi
  load_compose
  audit="$(new_evidence_dir a2-baseline-audit)"
  write_command "$audit" audit-a2-baseline "$resolved_evidence" "$iface"
  "${compose[@]}" run --rm --no-deps -T \
    -e "A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs" \
    -v "${audit}:/gate_evidence" \
    policy-runtime "$a2_test_script" motion-check "$iface" \
    2>&1 | tee "${audit}/motion_check.log"
  grep -q "service='ai_sport'" "${audit}/motion_check.log" ||
    die "official ai_sport is not restored after the interrupted baseline"
  {
    echo "result=PASS"
    echo "audit=operator_ctrl_c_after_hold_prone"
    echo "source_evidence=${resolved_evidence}"
    echo "stand_up_complete=true"
    echo "policy_active_observed=true"
    echo "controlled_down_complete=true"
    echo "policy_container_running=false"
    echo "official_mode=ai_sport"
  } > "${audit}/result.receipt"
  mark_pass a2-baseline "$audit"
}

cmd_piper_baseline() {
  load_session
  require_approval physical
  require_pass ros-readonly
  require_approval ros-readonly
  require_operator
  (( live_requested == 1 )) || die "PiPER standalone baseline requires explicit --live"
  [[ "${STAGE2_ALLOW_PIPER_BASELINE:-}" == "1" ]] || die "set STAGE2_ALLOW_PIPER_BASELINE=1 in this shell"
  load_compose
  local evidence
  evidence="$(new_evidence_dir piper-baseline)"
  set +e
  run_logged "$evidence" "${compose[@]}" run --rm --no-deps policy-runtime \
    ros2 run piper_bridge piper_smoke_test -- \
      --move \
      --resume-before-enable \
      --round-trip-target-rad 0.0 1.48 -0.63 -0.84 0.0 1.57 \
      --transition-s 10 \
      --hold-s 5 \
      --return-tolerance-deg 3.5 \
      --a2-remote-stop
  local status=$?
  set -e
  if (( status != 0 )); then
    record_failure piper-baseline "$evidence" "$status"
    exit "$status"
  fi
  {
    echo "operator=${operator}"
    echo "motion_authorization=STAGE2_ALLOW_PIPER_BASELINE=1 + --live"
    echo "resume_requested=true; operator_authorized=2026-08-24"
    echo "motion=10s smooth reach + 5s hold + 10s smooth return"
    echo "a2_remote=Select/B quick stop; L2+B controlled return"
    echo "target_rad=0.0,1.48,-0.63,-0.84,0.0,1.57"
  } > "${evidence}/operator.receipt"
  mark_pass piper-baseline "$evidence"
}

run_direct_shadow() {
  local step="$1"
  local run_seconds="$2"
  load_compose
  require_direct_files
  local evidence node_status_file piper_status_file status_status_file
  local a2_pid piper_pid status_pid node_pid
  evidence="$(new_evidence_dir "$step")"
  node_status_file="${evidence}/node.exit"
  piper_status_file="${evidence}/piper_no_command.exit"
  status_status_file="${evidence}/ready_status.exit"

  "${compose[@]}" run --rm --no-deps \
    -e A2_LOWCMD_TOPIC=/stage2_shadow/no_lowcmd \
    -e A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs \
    -v "${evidence}:/gate_evidence" \
    policy-runtime "$a2_test_script" no-lowcmd "$((run_seconds + 2))" \
    > "${evidence}/a2_no_lowcmd.log" 2>&1 &
  a2_pid=$!
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_DURATION=$((run_seconds + 2))" \
    policy-runtime bash -lc 'timeout "${STAGE2_GATE_DURATION}" ros2 topic echo --once /piper/joint_command' \
    > "${evidence}/piper_no_command.log" 2>&1 &
  piper_pid=$!
  (
    set +e
    "${compose[@]}" run --rm --no-deps \
      -e "STAGE2_GATE_DURATION=$((run_seconds + 2))" \
      policy-runtime bash -lc 'timeout "${STAGE2_GATE_DURATION}" ros2 topic echo /a2_piper_stage2/status std_msgs/msg/String --field data' \
      > "${evidence}/status.log" 2>&1
    echo "$?" > "$status_status_file"
  ) &
  status_pid=$!
  sleep 1
  (
    set +e
    "${compose[@]}" run --rm --no-deps \
      -e "STAGE2_GATE_DURATION=${run_seconds}" \
      -v "${params_file}:/stage2_direct.params.yaml:ro" \
      policy-runtime timeout --signal=INT "$run_seconds" \
      ros2 run a2_piper_stage2_direct a2_piper_stage2_direct \
      --ros-args --params-file /stage2_direct.params.yaml \
      -p lowcmd_topic:=/stage2_shadow/no_lowcmd \
      -p enable_motion:=false -p live_acknowledged:=false -p component_mode:=both \
      > "${evidence}/node.log" 2>&1
    echo "$?" > "$node_status_file"
  ) &
  node_pid=$!

  echo "Running ${step} for ${run_seconds}s; do not interrupt. Continuous status/no-command evidence is being recorded."

  set +e
  wait "$node_pid"
  wait "$status_pid"
  wait "$piper_pid"
  local piper_status=$?
  echo "$piper_status" > "$piper_status_file"
  wait "$a2_pid"
  local a2_status=$?
  set -e

  local node_status status_status
  node_status="$(<"$node_status_file")"
  status_status="$(<"$status_status_file")"
  if [[ "$node_status" != "124" ]]; then
    record_failure "$step" "$evidence" "$node_status"
    exit "$node_status"
  fi
  (( status_status == 124 )) ||
    { record_failure "$step" "$evidence" "$status_status"; exit "$status_status"; }
  grep -q -E 'contract=verified.*mode=shadow.*state=ready' "${evidence}/status.log" ||
    die "shadow never reached ready; inspect ${evidence}/status.log"
  if ! awk '
    /contract=verified.*mode=shadow.*state=ready/ { ready = 1 }
    ready && /state=blocked/ { exit 1 }
    END { if (!ready) exit 2 }
  ' "${evidence}/status.log"; then
    die "shadow entered blocked state after first ready; inspect ${evidence}/status.log"
  fi
  (( piper_status == 124 )) ||
    die "PiPER command appeared or observer failed; inspect ${evidence}/piper_no_command.log"
  (( a2_status == 0 )) || { record_failure "$step" "$evidence" "$a2_status"; exit "$a2_status"; }
  mark_pass "$step" "$evidence"
}

cmd_dry_run() {
  load_session
  require_pass a2-baseline
  require_approval a2-baseline
  require_pass piper-baseline
  require_approval piper-baseline
  duration="${duration:-60}"
  validate_duration
  run_direct_shadow dry-run "$duration"
}

cmd_joint_observe() {
  load_session
  require_pass dry-run
  require_approval dry-run
  duration="${duration:-600}"
  validate_duration
  load_compose
  local evidence
  evidence="$(new_evidence_dir joint-observe)"
  write_command "$evidence" docker compose run policy-runtime joint-observe "$duration"
  {
    printf 'component\tpolicy_joint\tbridge_or_raw_index\tobserved_positive_direction\tunit\tzero_reference\tobserved_lower\tobserved_upper\tstop_result\treviewer\tnotes\n'
    printf 'A2\tFL_hip_joint\t3\t\t\t\t\t\t\t\t\n'
    printf 'A2\tFR_hip_joint\t0\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRL_hip_joint\t9\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRR_hip_joint\t6\t\t\t\t\t\t\t\t\n'
    printf 'A2\tFL_thigh_joint\t4\t\t\t\t\t\t\t\t\n'
    printf 'A2\tFR_thigh_joint\t1\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRL_thigh_joint\t10\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRR_thigh_joint\t7\t\t\t\t\t\t\t\t\n'
    printf 'A2\tFL_calf_joint\t5\t\t\t\t\t\t\t\t\n'
    printf 'A2\tFR_calf_joint\t2\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRL_calf_joint\t11\t\t\t\t\t\t\t\t\n'
    printf 'A2\tRR_calf_joint\t8\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j1\tarm_j1\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j2\tarm_j2\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j3\tarm_j3\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j4\tarm_j4\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j5\tarm_j5\t\t\t\t\t\t\t\t\n'
    printf 'PiPER\tarm_j6\tarm_j6\t\t\t\t\t\t\t\t\n'
  } > "${evidence}/joint_validation_table.tsv"
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_DURATION=${duration}" \
    -e A2_LIVE_CLEAR_SCREEN=0 \
    -e A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs \
    -v "${evidence}:/gate_evidence" \
    policy-runtime bash -lc '
      set -uo pipefail
      a2=/opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh
      "$a2" joints-live "${STAGE2_GATE_DURATION}" 2>&1 |
        tee /gate_evidence/a2_joints_live.log &
      a2_pid=$!
      timeout "${STAGE2_GATE_DURATION}" ros2 topic echo /piper/joint_states 2>&1 |
        tee /gate_evidence/piper_joint_states.log &
      piper_pid=$!
      wait "${a2_pid}"
      a2_status=$?
      wait "${piper_pid}"
      piper_status=$?
      echo "a2_observer_exit=${a2_status}"
      echo "piper_observer_exit=${piper_status}"
      [[ "${a2_status}" == "0" ]] || exit "${a2_status}"
      [[ "${piper_status}" == "0" || "${piper_status}" == "124" ]] || exit 2
      [[ -s /gate_evidence/a2_joints_live.log ]] || exit 2
      [[ -s /gate_evidence/piper_joint_states.log ]] || exit 2
      for joint in arm_j1 arm_j2 arm_j3 arm_j4 arm_j5 arm_j6; do
        grep -q "${joint}" /gate_evidence/piper_joint_states.log || exit 2
      done
      echo "PASS: simultaneous read-only A2 and PiPER joint observation completed"
    ' 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure joint-observe "$evidence" "$status"; exit "$status"; }
  {
    echo "joint_observe_output_published=false"
    echo "external_approved_single_joint_program_may_publish=true"
    echo "a2_observer=${a2_test_script} joints-live"
    echo "piper_observer=ros2 topic echo /piper/joint_states"
    echo "human_table=${evidence}/joint_validation_table.tsv"
  } > "${evidence}/scope.receipt"
  mark_pass joint-observe "$evidence"
}

cmd_fault() {
  load_session
  require_pass joint-observe
  [[ "$scenario" == "process-stop" ]] ||
    die "only --scenario process-stop is automated; network/state/watchdog faults require the supervised site procedure in the runbook"
  duration="${duration:-30}"
  validate_duration
  load_compose
  require_direct_files
  local evidence node_pid container_name
  evidence="$(new_evidence_dir fault)"
  container_name="stage2-fault-${session_id}"
  "${compose[@]}" run --rm --name "$container_name" --no-deps \
    -v "${params_file}:/stage2_direct.params.yaml:ro" \
    policy-runtime ros2 run a2_piper_stage2_direct a2_piper_stage2_direct \
    --ros-args --params-file /stage2_direct.params.yaml \
    -p enable_motion:=false -p live_acknowledged:=false -p component_mode:=both \
    > "${evidence}/node.log" 2>&1 &
  node_pid=$!
  echo "$node_pid" > "${evidence}/node.pid"
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_DURATION=${duration}" \
    policy-runtime bash -lc 'timeout "${STAGE2_GATE_DURATION}" ros2 topic echo /a2_piper_stage2/status --field data' 2>&1 |
    tee "${evidence}/status.log" |
    grep -m1 -E 'contract=verified.*mode=shadow.*state=ready'
  local ready_status=${PIPESTATUS[2]}
  set -e
  if (( ready_status != 0 )); then
    docker stop --signal=SIGINT --time=5 "$container_name" >/dev/null 2>&1 || true
    wait "$node_pid" 2>/dev/null || true
    record_failure fault "$evidence" "$ready_status"
    exit "$ready_status"
  fi
  docker stop --signal=SIGINT --time=5 "$container_name" >/dev/null
  set +e
  wait "$node_pid"
  local node_status=$?
  set -e
  [[ "$node_status" == "0" || "$node_status" == "130" || "$node_status" == "137" ]] ||
    die "direct node did not stop cleanly after SIGINT; exit=${node_status}"
  {
    echo "scenario=process-stop"
    echo "scope=shadow process stop only"
    echo "not_proven=live A2/PiPER local watchdog behavior"
  } > "${evidence}/fault_scope.receipt"
  mark_pass fault "$evidence"
}

cmd_shadow() {
  load_session
  require_pass fault
  require_approval fault
  duration="${duration:-600}"
  validate_duration
  run_direct_shadow shadow "$duration"
}

cmd_live_preflight() {
  load_session
  require_operator
  validate_component
  require_pass shadow
  require_site_live_ready
  load_compose
  local evidence
  evidence="$(new_evidence_dir "live-preflight-${component}")"
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_OPERATOR=${operator}" \
    -e "STAGE2_GATE_COMPONENT=${component}" \
    -v "${params_file}:/stage2_direct.params.yaml:ro" \
    policy-runtime bash -lc '
      set -euo pipefail
      echo "operator=${STAGE2_GATE_OPERATOR}"
      echo "component=${STAGE2_GATE_COMPONENT}"
      echo "lowstate_type=$(ros2 topic type /lowstate)"
      echo "piper_state_type=$(ros2 topic type /piper/joint_states)"
      ros2 topic echo /lowstate --once --qos-reliability best_effort >/dev/null
      ros2 topic echo /piper/joint_states --once --qos-reliability best_effort >/dev/null
      if ros2 node list | grep -qx /a2_lowlevel_interface; then
        echo "another Stage2 direct node is already running" >&2
        exit 2
      fi
      /opt/stage2_ws/install/lib/a2_piper_stage2_direct/a2_piper_stage2_direct \
        --ros-args --params-file /stage2_direct.params.yaml \
        -p validate_live_site_only:=true \
        -p enable_motion:=false \
        -p live_acknowledged:=false \
        -p component_mode:="${STAGE2_GATE_COMPONENT}" \
        -p lowstate_topic:=/stage2_preflight/no_lowstate \
        -p lowcmd_topic:=/stage2_preflight/no_lowcmd \
        -p piper_state_topic:=/stage2_preflight/no_piper_state \
        -p piper_diagnostics_topic:=/stage2_preflight/no_piper_diagnostics \
        -p piper_command_topic:=/stage2_preflight/no_piper_command \
        -p piper_resume_service:=/stage2_preflight/no_piper_resume \
        -p piper_enable_service:=/stage2_preflight/no_piper_enable \
        -p piper_stop_service:=/stage2_preflight/no_piper_stop \
        -p trajectory_start_service:=/stage2_preflight/no_trajectory_start \
        -p status_topic:=/stage2_preflight/status \
        > /tmp/stage2_site_validation_node.log 2>&1 &
      validation_pid=$!
      set +e
      timeout 30 ros2 topic echo /stage2_preflight/status std_msgs/msg/String --field data 2>&1 |
        grep -m1 -E "contract=verified.*site=verified.*mode=site_validation.*state=ready"
      validation_status=${PIPESTATUS[1]}
      kill -INT "${validation_pid}" >/dev/null 2>&1
      wait "${validation_pid}"
      node_status=$?
      set -e
      if [[ "${validation_status}" != "0" ]]; then
        cat /tmp/stage2_site_validation_node.log >&2
        echo "canonical C++ live-site validation status was not observed" >&2
        exit 2
      fi
      if [[ "${node_status}" != "0" && "${node_status}" != "130" ]]; then
        cat /tmp/stage2_site_validation_node.log >&2
        echo "canonical C++ validation node stopped with exit=${node_status}" >&2
        exit "${node_status}"
      fi
      echo "PASS: canonical C++ live site loaded on isolated dummy topics"
      echo "PASS: live preflight sent no enable/resume/hardware output command"
    ' 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) || { record_failure "live-preflight-${component}" "$evidence" "$status"; exit "$status"; }
  mark_pass "live-preflight-${component}" "$evidence"
}

cmd_live() {
  load_session
  require_operator
  validate_component
  (( live_requested == 1 )) || die "live requires explicit --live"
  [[ "${STAGE2_ALLOW_LIVE:-}" == "1" ]] || die "set STAGE2_ALLOW_LIVE=1 in this shell"
  require_pass "live-preflight-${component}"
  require_approval "live-${component}"
  require_site_live_ready
  load_compose
  local evidence pid_file status container_name
  evidence="$(new_evidence_dir "live-${component}")"
  pid_file="${session_dir}/live.pid"
  container_name="stage2-live-${session_id}"
  if docker inspect -f '{{.State.Running}}' "$container_name" 2>/dev/null |
      grep -qx true; then
    die "Stage2 live container is already running: ${container_name}; do not launch live twice"
  fi
  if docker inspect "$container_name" >/dev/null 2>&1; then
    docker rm "$container_name" >/dev/null
  fi
  {
    echo "operator=${operator}"
    echo "component=${component}"
    echo "authorization=STAGE2_ALLOW_LIVE=1 + --live + human approval"
    echo "piper_resume_called=false"
    echo "site_modified=false"
    echo "container_name=${container_name}"
    echo "started_at=$(date --iso-8601=seconds)"
  } > "${evidence}/live_start.receipt"
  echo "WARNING: REAL MOTION PATH component=${component}"
  echo "First A automatically resumes/enables PiPER, then starts synchronized init interpolation. Keep the physical E-stop operator ready."
  echo "Stage2 handover: first A=measured hold/init interpolation; second A with centered sticks=30-frame warmup then policy."
  echo "PolicyActive keeps PiPER at init with a zero arm task command until an explicit arm-goal or trajectory command."
  echo "Normal stop: first L2+B=return to reset hold; A=warmup/resume; second L2+B=A2/PiPER rest interpolation, then PiPER stop."
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e A2_ALLOW_RELEASE_MODE=1 \
    -e A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs \
    -v "${evidence}:/gate_evidence" \
    policy-runtime bash -lc '
      set -euo pipefail
      a2=/opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh
      "$a2" motion-check "${A2_NET_IFACE}"
      "$a2" motion-release "${A2_NET_IFACE}"
    ' > >(tee "${evidence}/a2_motion_release.log") 2>&1
  local release_status=$?
  set -e
  (( release_status == 0 )) ||
    die "A2 MotionSwitcher release failed; Stage2 live node was not started. Evidence: ${evidence}"
  set +e
  "${compose[@]}" run --rm --name "$container_name" --no-deps \
    -v "${params_file}:/stage2_direct.params.yaml:ro" \
    policy-runtime ros2 run a2_piper_stage2_direct a2_piper_stage2_direct \
    --ros-args --params-file /stage2_direct.params.yaml \
    -p enable_motion:=true -p live_acknowledged:=true -p component_mode:="$component" \
    > >(tee "${evidence}/node.log") 2>&1 &
  local node_pid=$!
  echo "$node_pid" > "$pid_file"
  live_signal_cleanup() {
    trap - INT TERM
    echo "Live foreground interrupted: executing verified dual-path stop"
    "$0" stop --session "$session_id" --operator "$operator" \
      --reason live-foreground-interrupted || true
  }
  trap live_signal_cleanup INT TERM
  wait "$node_pid"
  status=$?
  trap - INT TERM
  set -e
  rm -f "$pid_file"
  {
    echo "ended_at=$(date --iso-8601=seconds)"
    echo "exit=${status}"
  } > "${evidence}/live_end.receipt"
  echo "Live process ended with exit=${status}; evidence: ${evidence}"
  return "$status"
}

cmd_arm_goal() {
  load_session
  require_operator
  require_approval live-both
  local number='^-?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$'
  [[ "$arm_radius" =~ $number ]] || die "--radius must be a finite decimal number"
  [[ "$arm_pitch" =~ $number ]] || die "--pitch must be a finite decimal number"
  [[ "$arm_yaw" =~ $number ]] || die "--yaw must be a finite decimal number"
  load_compose
  local container_name="stage2-live-${session_id}"
  docker inspect -f '{{.State.Running}}' "$container_name" 2>/dev/null |
    grep -qx true || die "Stage2 live container is not running: ${container_name}"
  local evidence
  evidence="$(new_evidence_dir arm-goal)"
  set +e
  "${compose[@]}" run --rm --no-deps policy-runtime \
    timeout 10 ros2 topic pub --once \
      /a2_piper_stage2/arm_goal \
      std_msgs/msg/Float64MultiArray \
      "{data: [${arm_radius}, ${arm_pitch}, ${arm_yaw}]}" \
      2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) ||
    die "arm goal publish failed; confirm second A reached PolicyActive. Evidence: ${evidence}"
  set +e
  "${compose[@]}" run --rm --no-deps policy-runtime \
    timeout 5 ros2 topic echo --once \
      /a2_piper_stage2/status --field data \
      > "${evidence}/status.log" 2>&1
  local status_echo=$?
  set -e
  (( status_echo == 0 )) &&
    grep -q 'arm_tracking=position' "${evidence}/status.log" ||
    die "arm goal was not accepted in PolicyActive. Evidence: ${evidence}"
  echo "PASS: arm position goal published [${arm_radius}, ${arm_pitch}, ${arm_yaw}]"
  echo "evidence: ${evidence}"
}

cmd_trajectory() {
  load_session
  require_operator
  require_approval live-both
  load_compose
  local container_name="stage2-live-${session_id}"
  docker inspect -f '{{.State.Running}}' "$container_name" 2>/dev/null |
    grep -qx true || die "Stage2 live container is not running: ${container_name}"
  local evidence
  evidence="$(new_evidence_dir trajectory)"
  set +e
  "${compose[@]}" run --rm --no-deps policy-runtime \
    timeout 10 ros2 service call \
      /a2_piper_stage2/start_arm_goal_trajectory \
      std_srvs/srv/Trigger '{}' 2>&1 | tee "${evidence}/stdout.log"
  local status=${PIPESTATUS[0]}
  set -e
  (( status == 0 )) && grep -q 'success=True' "${evidence}/stdout.log" ||
    die "trajectory trigger was rejected; confirm the second A reached PolicyActive. Evidence: ${evidence}"
  echo "PASS: round-trip arm-goal trajectory started"
  echo "evidence: ${evidence}"
}

cmd_stop() {
  load_session
  require_operator
  load_compose
  local evidence pid="" container_name="stage2-live-${session_id}"
  evidence="$(new_evidence_dir stop)"
  if [[ -f "${session_dir}/live.pid" ]]; then
    pid="$(<"${session_dir}/live.pid")"
  fi
  if docker inspect "$container_name" >/dev/null 2>&1; then
    docker stop --signal=SIGINT --time=5 "$container_name" > "${evidence}/container_stop.log" 2>&1 || true
  fi
  set +e
  "${compose[@]}" run --rm --no-deps policy-runtime \
    timeout 5 ros2 service call /piper/stop std_srvs/srv/Trigger '{}' \
    > "${evidence}/piper_stop.log" 2>&1
  local piper_status=$?
  "${compose[@]}" run --rm --no-deps \
    -e A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs \
    -e A2_ALLOW_ZERO_LOWCMD=1 \
    -v "${evidence}:/gate_evidence" \
    policy-runtime "$a2_test_script" zero-lowcmd 5 \
    > "${evidence}/a2_zero_lowcmd.log" 2>&1
  local a2_status=$?
  set -e
  {
    echo "operator=${operator}"
    echo "reason=${reason:-operator requested stop}"
    echo "time=$(date --iso-8601=seconds)"
    echo "live_pid=${pid:-not_running}"
    echo "piper_stop_exit=${piper_status}"
    echo "a2_zero_lowcmd_exit=${a2_status}"
  } > "${evidence}/stop.receipt"
  (( piper_status == 0 && a2_status == 0 )) ||
    die "software stop was not confirmed on both paths; use physical E-stop and inspect ${evidence}"
  echo "PASS: direct process interrupted, PiPER stop called, verified A2 zero-LowCmd path completed"
  echo "evidence: ${evidence}"
}

cmd_restore_a2() {
  load_session
  require_operator
  [[ -n "$iface" ]] || die "restore-a2 requires --iface NIC"
  [[ "${STAGE2_ALLOW_A2_RESTORE:-0}" == "1" ]] ||
    die "restore-a2 requires STAGE2_ALLOW_A2_RESTORE=1"
  load_compose
  local container_name="stage2-live-${session_id}"
  if docker inspect -f '{{.State.Running}}' "$container_name" 2>/dev/null |
      grep -qx true; then
    die "Stage2 live container is still running: ${container_name}"
  fi
  local evidence
  evidence="$(new_evidence_dir a2-restore)"
  write_command "$evidence" restore-a2 "$iface" "$operator"
  set +e
  "${compose[@]}" run --rm --no-deps \
    -e "STAGE2_GATE_IFACE=${iface}" \
    -e A2_ALLOW_SELECT_MODE=1 \
    -e A2_MOTION_RESTORE_MODE=ai_sport \
    -e A2_TEST_LOG_DIR=/gate_evidence/a2_wrapper_logs \
    -v "${evidence}:/gate_evidence" \
    policy-runtime bash -lc '
      set -euo pipefail
      a2=/opt/stage2/ros2/A2/scripts/a2_real_robot_test.sh
      "$a2" no-lowcmd 5
      "$a2" motion-restore "${STAGE2_GATE_IFACE}"
      "$a2" motion-check "${STAGE2_GATE_IFACE}"
    ' > >(tee "${evidence}/stdout.log") 2>&1
  local status=$?
  set -e
  if (( status != 0 )); then
    record_failure a2-restore "$evidence" "$status"
    return "$status"
  fi
  {
    echo "result=PASS"
    echo "action=a2-restore"
    echo "operator=${operator}"
    echo "target_mode=ai_sport"
    echo "time=$(date --iso-8601=seconds)"
  } > "${evidence}/result.receipt"
  echo "PASS: A2 official motion mode restored to ai_sport"
  echo "evidence: ${evidence}"
}

command="${1:-}"
[[ -n "$command" ]] || { usage; exit 2; }
shift
parse_options "$@"

case "$command" in
  init) cmd_init ;;
  status) show_status ;;
  next) show_next ;;
  approve) cmd_approve ;;
  offline) cmd_offline ;;
  network) cmd_network ;;
  ros-readonly) cmd_ros_readonly ;;
  a2-baseline) cmd_a2_baseline ;;
  audit-a2-baseline) cmd_audit_a2_baseline ;;
  piper-baseline) cmd_piper_baseline ;;
  dry-run) cmd_dry_run ;;
  joint-observe) cmd_joint_observe ;;
  fault) cmd_fault ;;
  shadow) cmd_shadow ;;
  live-preflight) cmd_live_preflight ;;
  live) cmd_live ;;
  arm-goal) cmd_arm_goal ;;
  trajectory) cmd_trajectory ;;
  stop) cmd_stop ;;
  restore-a2) cmd_restore_a2 ;;
  help|-h|--help) usage ;;
  *) die "unknown subcommand: ${command}" ;;
esac
