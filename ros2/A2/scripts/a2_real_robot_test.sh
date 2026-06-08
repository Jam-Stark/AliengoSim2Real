#!/usr/bin/env bash
set -eo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
ros_root="$repo_root/ros2"
observer="$script_dir/a2_real_robot_observer.py"
log_dir="${A2_TEST_LOG_DIR:-/tmp/a2_real_robot_tests}"
lowstate_topic="${A2_LOWSTATE_TOPIC:-/lowstate}"
lowcmd_topic="${A2_LOWCMD_TOPIC:-/lowcmd}"

mkdir -p "$log_dir"

source_if_present() {
  local setup_file="$1"
  if [ ! -f "$setup_file" ]; then
    return 0
  fi

  local nounset_was_on=0
  local errexit_was_on=0
  case "$-" in
    *u*) nounset_was_on=1 ;;
  esac
  case "$-" in
    *e*) errexit_was_on=1 ;;
  esac

  set +u
  set +e
  # shellcheck disable=SC1090
  source "$setup_file"
  local source_status=$?

  if [ "$nounset_was_on" -eq 1 ]; then
    set -u
  else
    set +u
  fi
  if [ "$errexit_was_on" -eq 1 ]; then
    set -e
  else
    set +e
  fi

  return "$source_status"
}

source_if_present /opt/ros/humble/setup.bash
source_if_present /opt/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash
source_if_present "$ros_root/install/setup.bash"

cd "$ros_root"

timestamp() {
  date +%Y%m%d_%H%M%S
}

log_file() {
  local name="$1"
  printf '%s/%s_%s.log\n' "$log_dir" "$name" "$(timestamp)"
}

print_log_path() {
  local log="$1"
  echo "[a2-real-test] log: $log"
}

run_logged() {
  local name="$1"
  shift
  local log
  log="$(log_file "$name")"
  print_log_path "$log"
  "$@" 2>&1 | tee "$log"
}

run_timeout_accept_124() {
  local name="$1"
  local duration="$2"
  shift 2
  local log status
  log="$(log_file "$name")"
  print_log_path "$log"
  set +e
  timeout "$duration" "$@" 2>&1 | tee "$log"
  status=${PIPESTATUS[0]}
  set -e
  if [ "$status" -eq 0 ] || [ "$status" -eq 124 ]; then
    echo "[a2-real-test] accepted exit=$status for timeout-driven command"
    return 0
  fi
  echo "[a2-real-test] command failed with exit=$status" >&2
  return "$status"
}

require_env_flag() {
  local name="$1"
  local explanation="$2"
  if [ "${!name:-}" != "1" ]; then
    echo "ERROR: set $name=1 to run this guarded path." >&2
    echo "Reason: $explanation" >&2
    exit 2
  fi
}

load_policy_run_config() {
  local config_path="${A2_POLICY_RUN_CONFIG:-$repo_root/ros2/A2/config/a2_policy_remote.env}"
  local override_file
  local source_status
  local allow_was_set=0
  local allow_value=""
  local names=(
    A2_POLICY_MAX_REMOTE_VX
    A2_POLICY_MAX_REMOTE_VY
    A2_POLICY_MAX_REMOTE_YAW
    A2_POLICY_REMOTE_DEADZONE
    A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY
    A2_POLICY_PUBLISH_AUX_DEBUG
    A2_POLICY_AUX_DEBUG_TOPIC
    A2_POLICY_AUX_EXPECTED_DIM
    A2_POLICY_AUX_PRINT_PERIOD
    A2_POLICY_BRAKE_GATE_ENABLED
    A2_POLICY_BRAKE_FORCE_X_THRESHOLD_N
    A2_POLICY_BRAKE_MIN_CMD_VX
    A2_POLICY_BRAKE_MAX_ABS_YAW
    A2_POLICY_BRAKE_HOLD_STEPS
  )
  local name

  if [ ! -f "$config_path" ]; then
    echo "ERROR: A2 policy run config not found: $config_path" >&2
    echo "Set A2_POLICY_RUN_CONFIG=/path/to/file.env to override." >&2
    exit 2
  fi
  if ! bash -n "$config_path"; then
    echo "ERROR: A2 policy run config has invalid bash syntax: $config_path" >&2
    exit 2
  fi

  override_file="$(mktemp)"
  for name in "${names[@]}"; do
    if [ "${!name+x}" = "x" ]; then
      printf 'export %s=%q\n' "$name" "${!name}" >> "$override_file"
    fi
  done

  if [ "${A2_ALLOW_ENABLE_MOTION+x}" = "x" ]; then
    allow_was_set=1
    allow_value="$A2_ALLOW_ENABLE_MOTION"
  fi

  A2_POLICY_MAX_REMOTE_VX=0.80
  A2_POLICY_MAX_REMOTE_VY=0.50
  A2_POLICY_MAX_REMOTE_YAW=0.6
  A2_POLICY_REMOTE_DEADZONE=0.08
  A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY=true
  A2_POLICY_PUBLISH_AUX_DEBUG=true
  A2_POLICY_AUX_DEBUG_TOPIC=/a2/policy_aux
  A2_POLICY_AUX_EXPECTED_DIM=6
  A2_POLICY_AUX_PRINT_PERIOD=0.2

  set +e
  # shellcheck disable=SC1090
  source "$config_path"
  source_status=$?
  set -e
  if [ "$source_status" -ne 0 ]; then
    rm -f "$override_file"
    echo "ERROR: failed to source A2 policy run config: $config_path" >&2
    exit "$source_status"
  fi

  # Operator-provided environment wins over repo defaults and config file.
  # A2_ALLOW_ENABLE_MOTION is intentionally not accepted from the config file.
  # shellcheck disable=SC1090
  source "$override_file"
  rm -f "$override_file"
  if [ "$allow_was_set" -eq 1 ]; then
    A2_ALLOW_ENABLE_MOTION="$allow_value"
    export A2_ALLOW_ENABLE_MOTION
  else
    unset A2_ALLOW_ENABLE_MOTION
  fi

  A2_POLICY_EFFECTIVE_CONFIG="$config_path"
}

print_policy_run_config() {
  echo "[a2-real-test] policy_run_config=${A2_POLICY_EFFECTIVE_CONFIG:-UNSET}"
  echo "[a2-real-test] remote_caps: vx=${A2_POLICY_MAX_REMOTE_VX} vy=${A2_POLICY_MAX_REMOTE_VY} yaw=${A2_POLICY_MAX_REMOTE_YAW}"
  echo "[a2-real-test] remote_deadzone=${A2_POLICY_REMOTE_DEADZONE}"
  echo "[a2-real-test] require_standup_before_policy=${A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY}"
  echo "[a2-real-test] publish_aux_debug=${A2_POLICY_PUBLISH_AUX_DEBUG} aux_debug_topic=${A2_POLICY_AUX_DEBUG_TOPIC}"
  echo "[a2-real-test] aux_expected_dim=${A2_POLICY_AUX_EXPECTED_DIM} aux_print_period=${A2_POLICY_AUX_PRINT_PERIOD}"
  echo "[a2-real-test] brake_gate=ignored/comment-only; not passed to a2_policy_deploy"
  echo "[a2-real-test] A2_ALLOW_ENABLE_MOTION is an operator env guard, not a config-file setting"
}

usage() {
  cat <<'USAGE'
Usage:
  a2_real_robot_test.sh connected-preflight IFACE
  a2_real_robot_test.sh lowstate [duration]
  a2_real_robot_test.sh joints [duration]
  a2_real_robot_test.sh joints-live [duration]
  a2_real_robot_test.sh no-lowcmd [duration]
  a2_real_robot_test.sh remote [duration]
  a2_real_robot_test.sh remote-live [duration]
  a2_real_robot_test.sh smoke-remote [duration]
  a2_real_robot_test.sh motion-check IFACE
  A2_ALLOW_RELEASE_MODE=1 a2_real_robot_test.sh motion-release IFACE
  A2_ALLOW_SELECT_MODE=1 a2_real_robot_test.sh motion-select IFACE MODE
  A2_ALLOW_SELECT_MODE=1 a2_real_robot_test.sh motion-restore IFACE
  A2_ALLOW_ZERO_LOWCMD=1 a2_real_robot_test.sh zero-lowcmd [duration]
  a2_real_robot_test.sh policy-listen-remote [duration]
  A2_ALLOW_ENABLE_MOTION=1 a2_real_robot_test.sh policy-enable-remote [duration]
  a2_real_robot_test.sh policy-aux-live [duration]
  a2_real_robot_test.sh policy-aux-monitor [duration]
  a2_real_robot_test.sh help

Run inside the A2 Docker container after entering with A2_NET_IFACE=<robot NIC>.
Logs are written under A2_TEST_LOG_DIR, default /tmp/a2_real_robot_tests.
Topic defaults: A2_LOWSTATE_TOPIC=/lowstate, A2_LOWCMD_TOPIC=/lowcmd.
no-lowcmd only observes the configured LowCmd topic and does not publish.
policy-enable-remote uses the two-A handover: first A starts stand-up, the node
holds the policy default pose, and second A starts warmup/policy handover.
joints-live and remote-live are live observe-only tools: they subscribe only to
the configured LowState topic and never publish LowCmd. Their duration defaults
to 0, meaning run until Ctrl-C.
Live env: A2_LIVE_PRINT_PERIOD=0.2, A2_LIVE_CLEAR_SCREEN=1,
A2_JOINT_MIN_DELTA=0.03, A2_REMOTE_DEADZONE=0.08.
Policy env: A2_POLICY_RUN_CONFIG defaults to ros2/A2/config/a2_policy_remote.env.
Operator env A2_POLICY_* overrides config values; A2_ALLOW_ENABLE_MOTION=1 must
be set only in the shell for policy-enable-remote, never in the config file.
policy-aux-live is an independent listen-only smoke. policy-aux-monitor only
subscribes to the active policy aux topic and never starts a policy node.
USAGE
}

connected_preflight() {
  local iface="${1:-}"
  if [ -z "$iface" ]; then
    echo "ERROR: connected-preflight requires IFACE, e.g. enp131s0" >&2
    exit 2
  fi

  run_logged connected_preflight bash -lc "
    set -eo pipefail
    echo \"pwd=\$(pwd)\"
    echo \"ROS_DISTRO=\${ROS_DISTRO:-UNSET}\"
    echo \"RMW_IMPLEMENTATION=\${RMW_IMPLEMENTATION:-UNSET}\"
    echo \"A2_NET_IFACE=\${A2_NET_IFACE:-UNSET}\"
    echo \"A2_LOWSTATE_TOPIC=${lowstate_topic}\"
    echo \"A2_LOWCMD_TOPIC=${lowcmd_topic}\"
    echo \"CYCLONEDDS_URI=\${CYCLONEDDS_URI:-UNSET}\"
    ip addr show '$iface'
    ping -c 5 192.168.123.161
    topics=\"\$(ros2 topic list)\"
    printf '%s\n' \"\$topics\"
    topic_visible() {
      local required=\"\$1\"
      local slash=\"/\${required#/}\"
      local bare=\"\${required#/}\"
      local topic
      while IFS= read -r topic; do
        if [ \"\$topic\" = \"\$required\" ] || [ \"\$topic\" = \"\$slash\" ] || [ \"\$topic\" = \"\$bare\" ]; then
          return 0
        fi
      done <<< \"\$topics\"
      return 1
    }
    topic_cli_name() {
      local required=\"\$1\"
      local slash=\"/\${required#/}\"
      local bare=\"\${required#/}\"
      local topic
      while IFS= read -r topic; do
        if [ \"\$topic\" = \"\$required\" ] || [ \"\$topic\" = \"\$slash\" ] || [ \"\$topic\" = \"\$bare\" ]; then
          printf '%s\n' \"\$topic\"
          return 0
        fi
      done <<< \"\$topics\"
      return 1
    }
    topic_info_if_visible() {
      local topic=\"\$1\"
      local actual=\"\"
      if actual=\"\$(topic_cli_name \"\$topic\")\"; then
        ros2 topic info \"\$actual\" -v || true
      else
        echo \"topic \$topic not visible; skipping ros2 topic info -v\"
      fi
    }
    require_topic_type() {
      local topic=\"\$1\"
      local expected=\"\$2\"
      local label=\"\$3\"
      local actual=\"\"
      local types=\"\"
      if ! actual=\"\$(topic_cli_name \"\$topic\")\"; then
        echo \"ERROR: required \$label topic \$topic is not visible.\" >&2
        exit 3
      fi
      if ! types=\"\$(ros2 topic type \"\$actual\" 2>&1)\"; then
        echo \"ERROR: failed to read type for \$label topic \$topic resolved as \$actual: \$types\" >&2
        exit 4
      fi
      printf 'configured_%s_topic=%s resolved_topic=%s types=%s\n' \"\$label\" \"\$topic\" \"\$actual\" \"\$types\"
      case \"\$types\" in
        *\"\$expected\"*) ;;
        *)
          echo \"ERROR: configured \$label topic \$topic resolved as \$actual does not include expected type \$expected; types=\$types\" >&2
          exit 4
          ;;
      esac
    }
    topic_info_if_visible '${lowstate_topic}'
    topic_info_if_visible /lf/lowstate
    echo 'INFO: /lf/lowstate is printed for diagnostics only; it is not the configured A2 backend default.'
    topic_info_if_visible '${lowcmd_topic}'
    if ! topic_visible '${lowstate_topic}'; then
      echo 'ERROR: required lowstate topic ${lowstate_topic} is not visible.' >&2
      exit 3
    fi
    if ! topic_visible '${lowcmd_topic}'; then
      echo 'ERROR: required lowcmd topic ${lowcmd_topic} is not visible.' >&2
      exit 3
    fi
    require_topic_type '${lowstate_topic}' unitree_hg/msg/LowState lowstate
    require_topic_type '${lowcmd_topic}' unitree_hg/msg/LowCmd lowcmd
    ros2 interface show unitree_hg/msg/LowState
    ros2 interface show unitree_hg/msg/LowCmd
    echo 'PASS: connected preflight topics visible and types match configured A2 backend topics'
  "
}

lowstate() {
  local duration="${1:-10}"
  run_logged lowstate python3 "$observer" lowstate "$duration" \
    --min-hz "${A2_LOWSTATE_MIN_HZ:-50}" \
    --max-gap-ms "${A2_LOWSTATE_MAX_GAP_MS:-250}" \
    --lowstate-topic "$lowstate_topic"
}

joints() {
  local duration="${1:-15}"
  local args=(
    joints "$duration"
    --print-period "${A2_JOINT_PRINT_PERIOD:-0.5}"
    --min-delta "${A2_JOINT_MIN_DELTA:-0.03}"
    --lowstate-topic "$lowstate_topic"
  )
  if [ -n "${A2_JOINT_CSV:-}" ]; then
    args+=(--csv "$A2_JOINT_CSV")
  fi
  run_logged joints python3 "$observer" "${args[@]}"
}

joints_live() {
  local duration="${1:-0}"
  local args=(
    joints-live "$duration"
    --print-period "${A2_LIVE_PRINT_PERIOD:-0.2}"
    --min-delta "${A2_JOINT_MIN_DELTA:-0.03}"
    --lowstate-topic "$lowstate_topic"
  )
  if [ "${A2_LIVE_CLEAR_SCREEN:-1}" = "0" ]; then
    args+=(--no-clear-screen)
  else
    args+=(--clear-screen)
  fi
  run_logged joints_live python3 "$observer" "${args[@]}"
}

no_lowcmd() {
  local duration="${1:-5}"
  run_logged no_lowcmd python3 "$observer" no-lowcmd "$duration" \
    --lowcmd-topic "$lowcmd_topic"
}

remote() {
  local duration="${1:-15}"
  local args=(
    remote "$duration"
    --deadzone "${A2_REMOTE_DEADZONE:-0.08}"
    --lowstate-topic "$lowstate_topic"
  )
  if [ "${A2_REMOTE_ALLOW_ZERO:-0}" = "1" ]; then
    args+=(--allow-zero)
  fi
  run_logged remote python3 "$observer" "${args[@]}"
}

remote_live() {
  local duration="${1:-0}"
  local args=(
    remote-live "$duration"
    --print-period "${A2_LIVE_PRINT_PERIOD:-0.2}"
    --deadzone "${A2_REMOTE_DEADZONE:-0.08}"
    --lowstate-topic "$lowstate_topic"
  )
  if [ "${A2_LIVE_CLEAR_SCREEN:-1}" = "0" ]; then
    args+=(--no-clear-screen)
  else
    args+=(--clear-screen)
  fi
  run_logged remote_live python3 "$observer" "${args[@]}"
}

smoke_remote() {
  local duration="${1:-15}"
  run_timeout_accept_124 smoke_remote "$duration" \
    ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
      -p lowstate_topic:="$lowstate_topic" \
      -p lowcmd_topic:="$lowcmd_topic" \
      -p log_remote:=true \
      -p remote_deadzone:="${A2_REMOTE_DEADZONE:-0.08}"
}

compile_motion_helper() {
  local helper_src="${A2_MOTION_HELPER_SRC:-/tmp/a2_motion_switcher_helper.cpp}"
  local helper_bin="${A2_MOTION_HELPER_BIN:-/tmp/a2_motion_switcher_helper}"
  local helper_wrapper="${A2_MOTION_HELPER_WRAPPER:-${helper_bin}.run}"
  local sdk_root="${UNITREE_SDK2_ROOT:-/opt/unitree/unitree_sdk2}"
  local arch
  arch="$(uname -m)"
  local arch_candidates=("$arch")
  local include_dirs=()
  local lib_dirs=()
  local sdk_lib_dir=""
  local helper_ld_path=""

  if [ "$arch" = "arm64" ]; then
    arch_candidates+=("aarch64")
  elif [ "$arch" = "aarch64" ]; then
    arch_candidates+=("arm64")
  fi

  add_include_dir() {
    local dir="$1"
    local existing
    [ -d "$dir" ] || return 0
    for existing in "${include_dirs[@]}"; do
      [ "$existing" = "$dir" ] && return 0
    done
    include_dirs+=("$dir")
  }

  add_lib_dir() {
    local dir="$1"
    local existing
    [ -d "$dir" ] || return 0
    for existing in "${lib_dirs[@]}"; do
      [ "$existing" = "$dir" ] && return 0
    done
    lib_dirs+=("$dir")
  }

  dir_has_lib() {
    local dir="$1"
    local lib_name="$2"
    [ -d "$dir" ] || return 1
    compgen -G "$dir/lib${lib_name}.*" >/dev/null
  }

  for candidate in \
    "$sdk_root/install/include" \
    "$sdk_root/install/include/ddscxx" \
    "$sdk_root/install/include/ddsc" \
    "$sdk_root/include" \
    "$sdk_root/include/ddscxx" \
    "$sdk_root/include/ddsc" \
    "$sdk_root/thirdparty/include" \
    "$sdk_root/thirdparty/include/ddscxx" \
    "$sdk_root/thirdparty/include/ddsc"; do
    add_include_dir "$candidate"
  done

  if [ "${#include_dirs[@]}" -eq 0 ]; then
    echo "ERROR: Unitree SDK2 include dir not found under $sdk_root" >&2
    return 2
  fi

  for candidate in \
    "$sdk_root/install/lib" \
    "$sdk_root/build/lib" \
    "$sdk_root/build"; do
    if dir_has_lib "$candidate" "unitree_sdk2"; then
      sdk_lib_dir="$candidate"
      break
    fi
  done
  if [ -z "$sdk_lib_dir" ]; then
    local arch_candidate
    for arch_candidate in "${arch_candidates[@]}"; do
      candidate="$sdk_root/lib/$arch_candidate"
      if dir_has_lib "$candidate" "unitree_sdk2"; then
        sdk_lib_dir="$candidate"
        break
      fi
    done
  fi
  if [ -z "$sdk_lib_dir" ] && dir_has_lib "$sdk_root/lib" "unitree_sdk2"; then
    sdk_lib_dir="$sdk_root/lib"
  fi
  if [ -z "$sdk_lib_dir" ]; then
    echo "ERROR: libunitree_sdk2 not found under $sdk_root" >&2
    return 2
  fi

  for candidate in \
    "$sdk_lib_dir" \
    "$sdk_root/install/lib" \
    "$sdk_root/build/lib" \
    "$sdk_root/build"; do
    add_lib_dir "$candidate"
  done
  for arch_candidate in "${arch_candidates[@]}"; do
    add_lib_dir "$sdk_root/lib/$arch_candidate"
    add_lib_dir "$sdk_root/thirdparty/lib/$arch_candidate"
  done
  add_lib_dir "$sdk_root/lib"
  add_lib_dir "$sdk_root/thirdparty/lib"

  local candidate
  for candidate in "${lib_dirs[@]}"; do
    if [ -z "$helper_ld_path" ]; then
      helper_ld_path="$candidate"
    else
      helper_ld_path="${helper_ld_path}:$candidate"
    fi
  done

  cat > "$helper_src" <<'CPP'
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <unitree/robot/b2/motion_switcher/motion_switcher_client.hpp>
#include <unitree/robot/channel/channel_factory.hpp>

namespace {

struct ChannelFactoryReleaseGuard {
  ~ChannelFactoryReleaseGuard() {
    std::cerr << "[a2-motion-helper] ChannelFactory::Release()" << std::endl;
    unitree::robot::ChannelFactory::Instance()->Release();
  }
};

struct ModeResult {
  int32_t ret = -1;
  std::string form;
  std::string name;
};

ModeResult check_mode(unitree::robot::b2::MotionSwitcherClient &client) {
  ModeResult result;
  std::cerr << "[a2-motion-helper] CheckMode()" << std::endl;
  result.ret = client.CheckMode(result.form, result.name);
  std::cout << "CheckMode ret=" << result.ret
            << " form='" << result.form << "'"
            << " name='" << result.name << "'" << std::endl;
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: a2_motion_switcher_helper check|release|select IFACE [mode|max_attempts]" << std::endl;
    return 2;
  }

  const std::string action = argv[1];
  const std::string iface = argv[2];

  std::cerr << "[a2-motion-helper] ChannelFactory::Init(domain=0, iface='"
            << iface << "')" << std::endl;
  unitree::robot::ChannelFactory::Instance()->Init(0, iface);
  ChannelFactoryReleaseGuard release_guard;

  std::cerr << "[a2-motion-helper] MotionSwitcherClient::Init()" << std::endl;
  unitree::robot::b2::MotionSwitcherClient client;
  client.SetTimeout(5.0f);
  client.Init();

  if (action == "check") {
    const ModeResult result = check_mode(client);
    return result.ret == 0 ? 0 : 3;
  }

  if (action == "release") {
    const int max_attempts = argc >= 4 ? std::stoi(argv[3]) : 5;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
      std::cout << "Release attempt " << attempt << "/" << max_attempts << std::endl;
      const ModeResult before = check_mode(client);
      if (before.ret != 0) {
        return 3;
      }
      if (before.name.empty()) {
        std::cout << "Motion mode already released." << std::endl;
        return 0;
      }

      const int32_t release_ret = client.ReleaseMode();
      std::cout << "ReleaseMode ret=" << release_ret << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(3));

      const ModeResult after = check_mode(client);
      if (after.ret == 0 && after.name.empty()) {
        std::cout << "Motion mode released." << std::endl;
        return 0;
      }
    }
    std::cerr << "ReleaseMode did not produce an empty CheckMode name within max attempts." << std::endl;
    return 4;
  }

  if (action == "select") {
    if (argc < 4 || std::string(argv[3]).empty()) {
      std::cerr << "Usage: a2_motion_switcher_helper select IFACE MODE" << std::endl;
      return 2;
    }
    const std::string mode = argv[3];
    std::cout << "Select target mode='" << mode << "'" << std::endl;
    const ModeResult before = check_mode(client);
    if (before.ret != 0) {
      return 3;
    }

    std::cerr << "[a2-motion-helper] SelectMode('" << mode << "')" << std::endl;
    const int32_t select_ret = client.SelectMode(mode);
    std::cout << "SelectMode('" << mode << "') ret=" << select_ret << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    const ModeResult after = check_mode(client);
    if (select_ret == 0 && after.ret == 0 && after.name == mode) {
      std::cout << "Motion mode selected: '" << after.name << "'" << std::endl;
      return 0;
    }

    std::cerr << "SelectMode did not produce expected CheckMode name='"
              << mode << "'; after ret=" << after.ret
              << " name='" << after.name << "'" << std::endl;
    return 4;
  }

  std::cerr << "Unknown action: " << action << std::endl;
  return 2;
}
CPP

  local compile_args=(
    g++ -std=c++17 "$helper_src" -o "$helper_bin"
  )
  for candidate in "${include_dirs[@]}"; do
    compile_args+=(-I"$candidate")
  done
  for candidate in "${lib_dirs[@]}"; do
    compile_args+=(-L"$candidate" -Wl,-rpath,"$candidate")
  done
  compile_args+=(-lunitree_sdk2 -lddscxx -lddsc -pthread)

  echo "[a2-real-test] compiling MotionSwitcher helper" >&2
  echo "[a2-real-test] sdk_root=$sdk_root" >&2
  echo "[a2-real-test] include_dirs:" >&2
  printf '[a2-real-test]   %s\n' "${include_dirs[@]}" >&2
  echo "[a2-real-test] lib_dirs:" >&2
  printf '[a2-real-test]   %s\n' "${lib_dirs[@]}" >&2
  echo "[a2-real-test] link_libs=-lunitree_sdk2 -lddscxx -lddsc -pthread" >&2
  echo "[a2-real-test] runtime LD_LIBRARY_PATH prefix=$helper_ld_path" >&2
  rm -f "$helper_bin" "$helper_wrapper"
  set +e
  "${compile_args[@]}" >&2
  local compile_status=$?
  set -e
  if [ "$compile_status" -ne 0 ]; then
    rm -f "$helper_bin" "$helper_wrapper"
    echo "ERROR: failed to compile MotionSwitcher helper from $helper_src" >&2
    echo "ERROR: check the SDK2 include/lib dirs above; DDS headers usually need install/include/ddscxx or thirdparty/include/ddscxx." >&2
    return "$compile_status"
  fi
  if command -v ldd >/dev/null 2>&1; then
    echo "[a2-real-test] ldd with SDK2 LD_LIBRARY_PATH prefix:" >&2
    LD_LIBRARY_PATH="${helper_ld_path}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$helper_bin" >&2 || true
  fi
  {
    printf '#!/usr/bin/env bash\n'
    printf 'export LD_LIBRARY_PATH=%q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n' "$helper_ld_path"
    printf 'exec %q "$@"\n' "$helper_bin"
  } > "$helper_wrapper"
  chmod +x "$helper_wrapper"
  echo "$helper_wrapper"
}

motion_check() {
  local iface="${1:-}"
  if [ -z "$iface" ]; then
    echo "ERROR: motion-check requires IFACE, e.g. enp131s0" >&2
    exit 2
  fi
  local helper
  if ! helper="$(compile_motion_helper)"; then
    echo "ERROR: motion-check cannot continue without a compiled MotionSwitcher helper." >&2
    exit 2
  fi
  run_logged motion_check "$helper" check "$iface"
}

motion_release() {
  local iface="${1:-}"
  if [ -z "$iface" ]; then
    echo "ERROR: motion-release requires IFACE, e.g. enp131s0" >&2
    exit 2
  fi
  require_env_flag A2_ALLOW_RELEASE_MODE "ReleaseMode closes Unitree built-in motion service before low-level control."
  local helper
  if ! helper="$(compile_motion_helper)"; then
    echo "ERROR: motion-release cannot continue without a compiled MotionSwitcher helper." >&2
    exit 2
  fi
  run_logged motion_release "$helper" release "$iface" "${A2_RELEASE_MAX_ATTEMPTS:-5}"
}

motion_select() {
  local iface="${1:-}"
  local mode="${2:-}"
  if [ -z "$iface" ] || [ -z "$mode" ]; then
    echo "ERROR: motion-select requires IFACE and MODE, e.g. enp131s0 ai_sport" >&2
    exit 2
  fi
  require_env_flag A2_ALLOW_SELECT_MODE "SelectMode restores a Unitree built-in motion service. Stop policy/LowCmd first, then run no-lowcmd until PASS before restoring."
  echo "WARNING: SelectMode('$mode') restores Unitree built-in motion service."
  echo "WARNING: stop a2_policy_deploy and every LowCmd publisher first."
  echo "WARNING: run 'A2/scripts/a2_real_robot_test.sh no-lowcmd 5' and require PASS before restoring."
  local helper
  if ! helper="$(compile_motion_helper)"; then
    echo "ERROR: motion-select cannot continue without a compiled MotionSwitcher helper." >&2
    exit 2
  fi
  run_logged motion_select "$helper" select "$iface" "$mode"
}

motion_restore() {
  local iface="${1:-}"
  if [ -z "$iface" ]; then
    echo "ERROR: motion-restore requires IFACE, e.g. enp131s0" >&2
    exit 2
  fi
  motion_select "$iface" "${A2_MOTION_RESTORE_MODE:-ai_sport}"
}

zero_lowcmd() {
  local duration="${1:-8}"
  require_env_flag A2_ALLOW_ZERO_LOWCMD "zero-lowcmd publishes explicit zero LowCmd frames to ${lowcmd_topic}."

  local obs_log smoke_status obs_status obs_pid
  obs_log="$(log_file zero_lowcmd_observer)"
  print_log_path "$obs_log"
  (
    python3 "$observer" lowcmd-crc "$duration" --expect-zero --expect-state-mode \
      --lowstate-topic "$lowstate_topic" --lowcmd-topic "$lowcmd_topic" 2>&1 | tee "$obs_log"
  ) &
  obs_pid=$!
  sleep 1

  run_timeout_accept_124 zero_lowcmd_smoke 3 \
    ros2 run a2_lowlevel a2_lowlevel_smoke --ros-args \
      -p lowstate_topic:="$lowstate_topic" \
      -p lowcmd_topic:="$lowcmd_topic" \
      -p publish_zero:=true \
      -p command_hz:=5.0
  smoke_status=$?

  set +e
  wait "$obs_pid"
  obs_status=$?
  set -e
  if [ "$obs_status" -ne 0 ]; then
    echo "ERROR: zero-lowcmd observer failed with exit=$obs_status" >&2
    return "$obs_status"
  fi
  return "$smoke_status"
}

policy_listen_remote() {
  local duration="${1:-20}"
  local obs_log obs_status obs_pid policy_status observer_duration
  load_policy_run_config
  print_policy_run_config
  observer_duration="$(python3 - "$duration" <<'PY'
import sys
print(f"{float(sys.argv[1]) + 2.0:g}")
PY
)"
  obs_log="$(log_file policy_listen_no_lowcmd)"
  print_log_path "$obs_log"
  echo "[a2-real-test] no-lowcmd observer duration=${observer_duration}s covers policy duration=${duration}s"
  (
    python3 "$observer" no-lowcmd "$observer_duration" \
      --lowcmd-topic "$lowcmd_topic" 2>&1 | tee "$obs_log"
  ) &
  obs_pid=$!
  sleep 1

  run_timeout_accept_124 policy_listen_remote "$duration" \
    ros2 run a2_lowlevel a2_policy_deploy --ros-args \
      -p lowstate_topic:="$lowstate_topic" \
      -p lowcmd_topic:="$lowcmd_topic" \
      -p enable_motion:=false \
      -p command_source:=remote \
      -p max_remote_vx:="$A2_POLICY_MAX_REMOTE_VX" \
      -p max_remote_vy:="$A2_POLICY_MAX_REMOTE_VY" \
      -p max_remote_yaw:="$A2_POLICY_MAX_REMOTE_YAW" \
      -p remote_deadzone:="$A2_POLICY_REMOTE_DEADZONE" \
      -p require_standup_before_policy:="$A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY" \
      -p publish_aux_debug:="$A2_POLICY_PUBLISH_AUX_DEBUG" \
      -p aux_debug_topic:="$A2_POLICY_AUX_DEBUG_TOPIC"
  policy_status=$?

  set +e
  wait "$obs_pid"
  obs_status=$?
  set -e
  if [ "$obs_status" -ne 0 ]; then
    echo "ERROR: no-lowcmd observer failed with exit=$obs_status" >&2
    return "$obs_status"
  fi
  return "$policy_status"
}

policy_enable_remote() {
  local duration="${1:-20}"
  load_policy_run_config
  print_policy_run_config
  require_env_flag A2_ALLOW_ENABLE_MOTION "enable_motion=true is a real motion path: first A publishes stand-up/hold LowCmd, second A starts handover warmup/policy."
  echo "WARNING: enable_motion=true publishes LowCmd only after first A starts stand-up."
  echo "WARNING: first A = stand-up interpolation; holder keeps policy default pose."
  echo "WARNING: second A = handover warmup, then policy active on the next valid cycle."
  echo "WARNING: L2 is not a locomotion gate; valid sticks map command directly after deadzone in PolicyActive."
  echo "WARNING: Select is the primary local stop; L2+B is only an additional stop path if L2 decodes correctly."
  run_timeout_accept_124 policy_enable_remote "$duration" \
    ros2 run a2_lowlevel a2_policy_deploy --ros-args \
      -p lowstate_topic:="$lowstate_topic" \
      -p lowcmd_topic:="$lowcmd_topic" \
      -p enable_motion:=true \
      -p command_source:=remote \
      -p max_remote_vx:="$A2_POLICY_MAX_REMOTE_VX" \
      -p max_remote_vy:="$A2_POLICY_MAX_REMOTE_VY" \
      -p max_remote_yaw:="$A2_POLICY_MAX_REMOTE_YAW" \
      -p remote_deadzone:="$A2_POLICY_REMOTE_DEADZONE" \
      -p require_standup_before_policy:="$A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY" \
      -p publish_aux_debug:="$A2_POLICY_PUBLISH_AUX_DEBUG" \
      -p aux_debug_topic:="$A2_POLICY_AUX_DEBUG_TOPIC"
}

policy_aux_live() {
  local duration="${1:-0}"
  local duration_info duration_mode observer_duration
  local obs_log policy_log obs_pid policy_pid obs_tail_pid policy_tail_pid
  local obs_status_file policy_status_file obs_status policy_status

  load_policy_run_config
  print_policy_run_config

  if ! duration_info="$(python3 - "$duration" <<'PY'
import math
import sys

try:
    duration = float(sys.argv[1])
except ValueError:
    print("ERROR: duration must be numeric", file=sys.stderr)
    sys.exit(2)
if not math.isfinite(duration) or duration < 0.0:
    print("ERROR: duration must be finite and non-negative", file=sys.stderr)
    sys.exit(2)
if duration == 0.0:
    print("live 31536000")
else:
    print(f"finite {duration + 2.0:g}")
PY
)"; then
    exit 2
  fi
  duration_mode="${duration_info%% *}"
  observer_duration="${duration_info#* }"

  obs_log="$(log_file policy_aux_live_no_lowcmd)"
  policy_log="$(log_file policy_aux_live)"
  obs_status_file="$(mktemp)"
  policy_status_file="$(mktemp)"
  print_log_path "$obs_log"
  print_log_path "$policy_log"
  echo "[a2-real-test] policy-aux-live duration=${duration}s mode=${duration_mode}; no-lowcmd observer duration=${observer_duration}s"

  terminate_process_tree() {
    local pid="${1:-}"
    if [ -z "$pid" ]; then
      return 0
    fi
    if ! kill -0 "$pid" >/dev/null 2>&1; then
      return 0
    fi
    if command -v pkill >/dev/null 2>&1; then
      pkill -TERM -P "$pid" >/dev/null 2>&1 || true
    fi
    kill "$pid" >/dev/null 2>&1 || true
    sleep 0.2
    if kill -0 "$pid" >/dev/null 2>&1; then
      if command -v pkill >/dev/null 2>&1; then
        pkill -KILL -P "$pid" >/dev/null 2>&1 || true
      fi
      kill -KILL "$pid" >/dev/null 2>&1 || true
    fi
  }

  cleanup_policy_aux_live() {
    local exit_code=$?
    trap - INT TERM EXIT
    terminate_process_tree "${policy_pid:-}"
    terminate_process_tree "${obs_pid:-}"
    terminate_process_tree "${policy_tail_pid:-}"
    terminate_process_tree "${obs_tail_pid:-}"
    wait "$policy_pid" >/dev/null 2>&1 || true
    wait "$obs_pid" >/dev/null 2>&1 || true
    wait "$policy_tail_pid" >/dev/null 2>&1 || true
    wait "$obs_tail_pid" >/dev/null 2>&1 || true
    rm -f "$obs_status_file" "$policy_status_file"
    exit "$exit_code"
  }
  trap cleanup_policy_aux_live INT TERM EXIT

  : > "$obs_log"
  : > "$policy_log"
  tail -n +1 -f "$obs_log" &
  obs_tail_pid=$!
  tail -n +1 -f "$policy_log" &
  policy_tail_pid=$!

  (
    set +e
    python3 "$observer" no-lowcmd "$observer_duration" \
      --lowcmd-topic "$lowcmd_topic"
    echo "$?" > "$obs_status_file"
  ) > "$obs_log" 2>&1 &
  obs_pid=$!
  sleep 1

  (
    set +e
    if [ "$duration_mode" = "finite" ]; then
      timeout "$duration" ros2 run a2_lowlevel a2_policy_deploy --ros-args \
        -p lowstate_topic:="$lowstate_topic" \
        -p lowcmd_topic:="$lowcmd_topic" \
        -p enable_motion:=false \
        -p command_source:=remote \
        -p monitor_policy_aux:=true \
        -p max_remote_vx:="$A2_POLICY_MAX_REMOTE_VX" \
        -p max_remote_vy:="$A2_POLICY_MAX_REMOTE_VY" \
        -p max_remote_yaw:="$A2_POLICY_MAX_REMOTE_YAW" \
        -p remote_deadzone:="$A2_POLICY_REMOTE_DEADZONE" \
        -p require_standup_before_policy:="$A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY" \
        -p publish_aux_debug:="$A2_POLICY_PUBLISH_AUX_DEBUG" \
        -p aux_debug_topic:="$A2_POLICY_AUX_DEBUG_TOPIC" \
        -p policy_aux_expected_dim:="$A2_POLICY_AUX_EXPECTED_DIM" \
        -p policy_aux_print_period_sec:="$A2_POLICY_AUX_PRINT_PERIOD"
    else
      ros2 run a2_lowlevel a2_policy_deploy --ros-args \
        -p lowstate_topic:="$lowstate_topic" \
        -p lowcmd_topic:="$lowcmd_topic" \
        -p enable_motion:=false \
        -p command_source:=remote \
        -p monitor_policy_aux:=true \
        -p max_remote_vx:="$A2_POLICY_MAX_REMOTE_VX" \
        -p max_remote_vy:="$A2_POLICY_MAX_REMOTE_VY" \
        -p max_remote_yaw:="$A2_POLICY_MAX_REMOTE_YAW" \
        -p remote_deadzone:="$A2_POLICY_REMOTE_DEADZONE" \
        -p require_standup_before_policy:="$A2_POLICY_REQUIRE_STANDUP_BEFORE_POLICY" \
        -p publish_aux_debug:="$A2_POLICY_PUBLISH_AUX_DEBUG" \
        -p aux_debug_topic:="$A2_POLICY_AUX_DEBUG_TOPIC" \
        -p policy_aux_expected_dim:="$A2_POLICY_AUX_EXPECTED_DIM" \
        -p policy_aux_print_period_sec:="$A2_POLICY_AUX_PRINT_PERIOD"
    fi
    echo "$?" > "$policy_status_file"
  ) > "$policy_log" 2>&1 &
  policy_pid=$!

  if [ "$duration_mode" = "live" ]; then
    while true; do
      if ! kill -0 "$obs_pid" >/dev/null 2>&1; then
        set +e
        wait "$obs_pid"
        set -e
        obs_status="$(cat "$obs_status_file" 2>/dev/null || echo 130)"
        terminate_process_tree "$policy_pid"
        wait "$policy_pid" >/dev/null 2>&1 || true
        trap - INT TERM EXIT
        terminate_process_tree "$policy_tail_pid"
        terminate_process_tree "$obs_tail_pid"
        wait "$policy_tail_pid" >/dev/null 2>&1 || true
        wait "$obs_tail_pid" >/dev/null 2>&1 || true
        rm -f "$obs_status_file" "$policy_status_file"
        if [ "$obs_status" -ne 0 ]; then
          echo "ERROR: no-lowcmd observer failed with exit=$obs_status" >&2
          return "$obs_status"
        fi
        return 0
      fi
      if ! kill -0 "$policy_pid" >/dev/null 2>&1; then
        set +e
        wait "$policy_pid"
        set -e
        policy_status="$(cat "$policy_status_file" 2>/dev/null || echo 130)"
        terminate_process_tree "$obs_pid"
        wait "$obs_pid" >/dev/null 2>&1 || true
        obs_status="$(cat "$obs_status_file" 2>/dev/null || echo 130)"
        trap - INT TERM EXIT
        terminate_process_tree "$policy_tail_pid"
        terminate_process_tree "$obs_tail_pid"
        wait "$policy_tail_pid" >/dev/null 2>&1 || true
        wait "$obs_tail_pid" >/dev/null 2>&1 || true
        rm -f "$obs_status_file" "$policy_status_file"
        if [ "$obs_status" -ne 0 ] && [ "$obs_status" -ne 143 ] && [ "$obs_status" -ne 130 ]; then
          echo "ERROR: no-lowcmd observer failed with exit=$obs_status" >&2
          return "$obs_status"
        fi
        return "$policy_status"
      fi
      sleep 0.2
    done
  fi

  set +e
  wait "$policy_pid"
  set -e
  policy_status="$(cat "$policy_status_file" 2>/dev/null || echo 130)"

  set +e
  wait "$obs_pid"
  set -e
  obs_status="$(cat "$obs_status_file" 2>/dev/null || echo 130)"

  trap - INT TERM EXIT
  terminate_process_tree "$policy_tail_pid"
  terminate_process_tree "$obs_tail_pid"
  wait "$policy_tail_pid" >/dev/null 2>&1 || true
  wait "$obs_tail_pid" >/dev/null 2>&1 || true
  rm -f "$obs_status_file" "$policy_status_file"

  if [ "$obs_status" -ne 0 ]; then
    echo "ERROR: no-lowcmd observer failed with exit=$obs_status" >&2
    return "$obs_status"
  fi
  if [ "$policy_status" -eq 0 ] || [ "$policy_status" -eq 124 ]; then
    echo "[a2-real-test] accepted policy-aux-live exit=$policy_status"
    return 0
  fi
  echo "ERROR: policy-aux-live failed with exit=$policy_status" >&2
  return "$policy_status"
}

policy_aux_monitor() {
  local duration="${1:-0}"
  load_policy_run_config
  print_policy_run_config
  local args=(
    policy-aux-topic-live "$duration"
    --topic "$A2_POLICY_AUX_DEBUG_TOPIC"
    --expected-dim "$A2_POLICY_AUX_EXPECTED_DIM"
    --print-period "$A2_POLICY_AUX_PRINT_PERIOD"
  )
  if [ "${A2_LIVE_CLEAR_SCREEN:-1}" = "0" ]; then
    args+=(--no-clear-screen)
  else
    args+=(--clear-screen)
  fi
  run_logged policy_aux_monitor python3 "$observer" "${args[@]}"
}

command="${1:-help}"
shift || true

case "$command" in
  connected-preflight)
    connected_preflight "$@"
    ;;
  lowstate)
    lowstate "$@"
    ;;
  joints)
    joints "$@"
    ;;
  joints-live)
    joints_live "$@"
    ;;
  no-lowcmd)
    no_lowcmd "$@"
    ;;
  remote)
    remote "$@"
    ;;
  remote-live)
    remote_live "$@"
    ;;
  smoke-remote)
    smoke_remote "$@"
    ;;
  motion-check)
    motion_check "$@"
    ;;
  motion-release)
    motion_release "$@"
    ;;
  motion-select)
    motion_select "$@"
    ;;
  motion-restore)
    motion_restore "$@"
    ;;
  zero-lowcmd)
    zero_lowcmd "$@"
    ;;
  policy-listen-remote)
    policy_listen_remote "$@"
    ;;
  policy-enable-remote)
    policy_enable_remote "$@"
    ;;
  policy-aux-live)
    policy_aux_live "$@"
    ;;
  policy-aux-monitor)
    policy_aux_monitor "$@"
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "ERROR: unknown subcommand: $command" >&2
    usage >&2
    exit 2
    ;;
esac
