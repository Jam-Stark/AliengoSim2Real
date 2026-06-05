#!/usr/bin/env bash
set -u

UNITREE_ROOT="/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree"
DO_PING=0
NO_SENSITIVE=1

usage() {
  cat <<'EOF'
Usage: collect_deploy_machine_info.sh [options]

Collect deployment-machine diagnostics for the A2 deployment chain and write a
Markdown report to stdout.

Options:
  --unitree-root <path>  Unitree source root. Default: /home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree
  --ping                Run short ping checks for Unitree/A2 network addresses
  --no-sensitive        Avoid sensitive env dumps. This is the default behavior
  --help                Show this help

Example:
  bash ros2/A2/scripts/collect_deploy_machine_info.sh > DeployMachineINFO.md
  bash ros2/A2/scripts/collect_deploy_machine_info.sh --ping > DeployMachineINFO.md
EOF
}

expand_path() {
  case "$1" in
    "~") printf '%s\n' "${HOME}" ;;
    "~/"*) printf '%s/%s\n' "${HOME}" "${1#~/}" ;;
    *) printf '%s\n' "$1" ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --unitree-root)
      if [ "$#" -lt 2 ]; then
        printf 'FAILED: --unitree-root requires a path\n' >&2
        exit 2
      fi
      UNITREE_ROOT="$(expand_path "$2")"
      shift 2
      ;;
    --ping)
      DO_PING=1
      shift
      ;;
    --no-sensitive)
      NO_SENSITIVE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      printf 'FAILED: unknown argument: %s\n\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

SCRIPT_SOURCE="${BASH_SOURCE[0]:-$0}"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SOURCE")" 2>/dev/null && pwd -P || pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename "$SCRIPT_SOURCE")"
A2_DIR="$(cd "${SCRIPT_DIR}/.." 2>/dev/null && pwd -P || printf 'UNAVAILABLE')"
REPO_ROOT="UNAVAILABLE"
if [ "$A2_DIR" != "UNAVAILABLE" ]; then
  REPO_ROOT="$(cd "${A2_DIR}/../.." 2>/dev/null && pwd -P || printf 'UNAVAILABLE')"
fi

command_path() {
  if command -v "$1" >/dev/null 2>&1; then
    command -v "$1"
  else
    printf 'MISSING'
  fi
}

section() {
  printf '\n## %s\n\n' "$1"
}

subsection() {
  printf '\n### %s\n\n' "$1"
}

run_shell() {
  title="$1"
  cmd="$2"
  subsection "$title"
  printf '```text\n'
  printf '$ %s\n' "$cmd"
  output="$(bash -c "$cmd" 2>&1)"
  status=$?
  if [ -n "$output" ]; then
    printf '%s\n' "$output"
  else
    printf 'UNAVAILABLE: no output\n'
  fi
  if [ "$status" -ne 0 ]; then
    printf 'FAILED: exit %s\n' "$status"
  fi
  printf '```\n'
}

print_kv() {
  printf -- '- %s: `%s`\n' "$1" "$2"
}

safe_oneline() {
  value="$1"
  limit="${2:-600}"
  if [ -z "$value" ]; then
    printf 'UNSET'
    return
  fi
  printf '%s' "$value" | tr '\r\n\t' '   ' | sed 's/[[:cntrl:]]/?/g' | cut -c "1-${limit}"
}

env_summary() {
  name="$1"
  value="${!name-}"
  if [ -z "$value" ]; then
    print_kv "$name" "UNSET"
    return
  fi
  length="$(printf '%s' "$value" | wc -c | tr -d ' ')"
  preview="$(safe_oneline "$value" 600)"
  suffix=""
  if [ "$length" -gt 600 ] 2>/dev/null; then
    suffix=" ...TRUNCATED"
  fi
  print_kv "$name" "SET length=${length} preview=${preview}${suffix}"
}

version_report() {
  cmd="$1"
  version_args="${2:---version}"
  subsection "$cmd"
  printf '```text\n'
  path="$(command_path "$cmd")"
  printf 'path: %s\n' "$path"
  if [ "$path" = "MISSING" ]; then
    printf 'MISSING\n'
  else
    output="$("$cmd" $version_args 2>&1)"
    status=$?
    if [ -n "$output" ]; then
      printf '%s\n' "$output" | sed -n '1,8p'
    else
      printf 'UNAVAILABLE: no version output\n'
    fi
    if [ "$status" -ne 0 ]; then
      printf 'FAILED: exit %s\n' "$status"
    fi
  fi
  printf '```\n'
}

file_status() {
  label="$1"
  path="$2"
  if [ -e "$path" ]; then
    printf -- '- %s: `FOUND` `%s`\n' "$label" "$path"
  else
    printf -- '- %s: `MISSING` `%s`\n' "$label" "$path"
  fi
}

git_repo_report() {
  name="$1"
  path="$2"
  subsection "$name"
  printf -- '- path: `%s`\n' "$path"
  if [ ! -d "$path" ]; then
    printf -- '- exists: `MISSING`\n'
    return
  fi
  printf -- '- exists: `FOUND`\n'
  if ! command -v git >/dev/null 2>&1; then
    printf -- '- git: `MISSING`\n'
    return
  fi
  if ! git -C "$path" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf -- '- git repo: `UNAVAILABLE`\n'
    return
  fi
  branch="$(git -C "$path" rev-parse --abbrev-ref HEAD 2>/dev/null || printf 'FAILED')"
  commit="$(git -C "$path" rev-parse --short HEAD 2>/dev/null || printf 'FAILED')"
  dirty="$(git -C "$path" status --porcelain 2>/dev/null || printf 'FAILED')"
  if [ -z "$dirty" ]; then
    dirty="clean"
  else
    dirty="dirty"
  fi
  printf -- '- branch: `%s`\n' "$branch"
  printf -- '- commit: `%s`\n' "$commit"
  printf -- '- dirty status: `%s`\n' "$dirty"
  printf -- '- remotes:\n'
  printf '```text\n'
  remotes="$(git -C "$path" remote -v 2>&1)"
  if [ -n "$remotes" ]; then
    printf '%s\n' "$remotes"
  else
    printf 'UNAVAILABLE: no remotes\n'
  fi
  printf '```\n'
}

ping_host() {
  host="$1"
  subsection "ping ${host}"
  printf '```text\n'
  if ! command -v ping >/dev/null 2>&1; then
    printf 'MISSING: ping\n'
    printf '```\n'
    return
  fi
  printf '$ ping -c 1 -W 1 %s\n' "$host"
  output="$(ping -c 1 -W 1 "$host" 2>&1)"
  status=$?
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "$output"
    printf 'FAILED: exit %s\n' "$status"
    printf '```\n'
    return
  fi
  printf '%s\n' "$output"
  printf '```\n'
}

ros_pkg_prefix() {
  pkg="$1"
  subsection "ros2 pkg prefix ${pkg}"
  printf '```text\n'
  if ! command -v ros2 >/dev/null 2>&1; then
    printf 'MISSING: ros2\n'
    printf '```\n'
    return
  fi
  printf '$ ros2 pkg prefix %s\n' "$pkg"
  output="$(ros2 pkg prefix "$pkg" 2>&1)"
  status=$?
  if [ -n "$output" ]; then
    printf '%s\n' "$output"
  else
    printf 'UNAVAILABLE: no output\n'
  fi
  if [ "$status" -ne 0 ]; then
    printf 'FAILED: exit %s\n' "$status"
  fi
  printf '```\n'
}

ros_interface_show() {
  iface="$1"
  subsection "ros2 interface show ${iface}"
  printf '```text\n'
  if ! command -v ros2 >/dev/null 2>&1; then
    printf 'MISSING: ros2\n'
    printf '```\n'
    return
  fi
  printf '$ ros2 interface show %s\n' "$iface"
  output="$(ros2 interface show "$iface" 2>&1)"
  status=$?
  if [ -n "$output" ]; then
    printf '%s\n' "$output" | sed -n '1,220p'
    line_count="$(printf '%s\n' "$output" | wc -l | tr -d ' ')"
    if [ "$line_count" -gt 220 ] 2>/dev/null; then
      printf '...TRUNCATED after 220 lines\n'
    fi
  else
    printf 'UNAVAILABLE: no output\n'
  fi
  if [ "$status" -ne 0 ]; then
    printf 'FAILED: exit %s\n' "$status"
  fi
  printf '```\n'
}

printf '# A2 Deploy Machine Info\n'

section "Report Metadata"
print_kv "timestamp" "$(date '+%Y-%m-%d %H:%M:%S %Z' 2>/dev/null || printf 'FAILED')"
print_kv "hostname" "$(hostname 2>/dev/null || printf 'FAILED')"
print_kv "user" "$(id -un 2>/dev/null || whoami 2>/dev/null || printf 'FAILED')"
print_kv "cwd" "$(pwd 2>/dev/null || printf 'FAILED')"
print_kv "script path" "$SCRIPT_PATH"
print_kv "unitree root" "$UNITREE_ROOT"
print_kv "no-sensitive mode" "$NO_SENSITIVE"

section "OS / Kernel / Arch"
if [ -f /etc/os-release ]; then
  run_shell "/etc/os-release" "cat /etc/os-release"
else
  subsection "/etc/os-release"
  printf '```text\nUNAVAILABLE: /etc/os-release not found\n```\n'
  if command -v sw_vers >/dev/null 2>&1; then
    run_shell "macOS sw_vers" "sw_vers"
  fi
fi
run_shell "uname" "uname -a"
print_kv "CPU arch" "$(uname -m 2>/dev/null || printf 'FAILED')"
print_kv "LONG_BIT" "$(getconf LONG_BIT 2>/dev/null || printf 'UNAVAILABLE')"

section "Network"
run_shell "interface list" "if command -v ip >/dev/null 2>&1; then ip -brief link; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a; else echo 'MISSING: ip and ifconfig'; fi"
run_shell "IPv4 addresses" "if command -v ip >/dev/null 2>&1; then ip -4 addr show; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a | grep -E 'inet '; else echo 'MISSING: ip and ifconfig'; fi"
run_shell "default route" "if command -v ip >/dev/null 2>&1; then ip route show default; elif command -v route >/dev/null 2>&1; then route -n get default; elif command -v netstat >/dev/null 2>&1; then netstat -rn | sed -n '1,20p'; else echo 'MISSING: ip, route, and netstat'; fi"
run_shell "Unitree subnet presence" "if command -v ip >/dev/null 2>&1; then ip -4 addr show; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a; else echo 'MISSING: ip and ifconfig'; fi | grep -E '192\\.168\\.(123|124)\\.' || echo 'UNAVAILABLE: no 192.168.123/124 address detected'"
if [ "$DO_PING" -eq 1 ]; then
  ping_host "192.168.123.161"
  ping_host "192.168.123.162"
  ping_host "192.168.124.162"
else
  print_kv "ping checks" "SKIPPED (pass --ping to run)"
fi

section "ROS2"
if [ -d /opt/ros ]; then
  print_kv "/opt/ros distros" "$(find /opt/ros -mindepth 1 -maxdepth 1 -type d -exec basename {} \; 2>/dev/null | sort | tr '\n' ' ' | sed 's/[[:space:]]*$//' || printf 'FAILED')"
else
  print_kv "/opt/ros distros" "UNAVAILABLE"
fi
print_kv "ROS_DISTRO" "${ROS_DISTRO:-UNSET}"
print_kv "RMW_IMPLEMENTATION" "${RMW_IMPLEMENTATION:-UNSET}"
env_summary "CYCLONEDDS_URI"
version_report "ros2" "--version"
version_report "colcon" "--version"

section "Unitree Repositories"
git_repo_report "unitree_ros2" "${UNITREE_ROOT}/unitree_ros2"
git_repo_report "unitree_sdk2" "${UNITREE_ROOT}/unitree_sdk2"
git_repo_report "unitree_sdk2_python" "${UNITREE_ROOT}/unitree_sdk2_python"

subsection "Unitree key files"
file_status "unitree_ros2/setup.sh" "${UNITREE_ROOT}/unitree_ros2/setup.sh"
file_status "unitree_ros2/setup_local.sh" "${UNITREE_ROOT}/unitree_ros2/setup_local.sh"
file_status "unitree_ros2/cyclonedds_ws/install/setup.bash" "${UNITREE_ROOT}/unitree_ros2/cyclonedds_ws/install/setup.bash"
file_status "unitree_sdk2/include/unitree" "${UNITREE_ROOT}/unitree_sdk2/include/unitree"

section "ROS2 Packages / Interfaces"
ros_pkg_prefix "unitree_hg"
ros_pkg_prefix "unitree_go"
ros_pkg_prefix "unitree_api"
ros_interface_show "unitree_hg/msg/LowCmd"
ros_interface_show "unitree_hg/msg/LowState"
ros_interface_show "unitree_hg/msg/MotorCmd"
ros_interface_show "unitree_hg/msg/LowCmd_"
ros_interface_show "unitree_hg/msg/LowState_"
ros_interface_show "unitree_hg/msg/MotorCmd_"

section "Build Tools"
version_report "cmake" "--version"
version_report "gcc" "--version"
version_report "g++" "--version"
version_report "make" "--version"
version_report "python3" "--version"
version_report "pip3" "--version"
version_report "git" "--version"

section "Runtime Libraries / Hardware"
run_shell "ldconfig Unitree-related libs" "if command -v ldconfig >/dev/null 2>&1; then ldconfig -p 2>/dev/null | grep -E 'onnxruntime|cyclonedds|unitree' || echo 'UNAVAILABLE: no matching libs in ldconfig cache'; else echo 'MISSING: ldconfig'; fi"
run_shell "NVIDIA GPU" "if command -v nvidia-smi >/dev/null 2>&1; then nvidia-smi; else echo 'MISSING: nvidia-smi'; fi"
subsection "USB / network command availability"
printf '```text\n'
for cmd in lsusb lspci ip ifconfig route netstat nmcli ethtool tcpdump ping; do
  printf '%-10s %s\n' "$cmd" "$(command_path "$cmd")"
done
printf '```\n'

section "A2 Package Readiness"
print_kv "repo root candidate" "$REPO_ROOT"
print_kv "A2 package dir" "$A2_DIR"
if [ "$A2_DIR" != "UNAVAILABLE" ]; then
  file_status "ros2/A2/package.xml" "${A2_DIR}/package.xml"
  file_status "ros2/A2/CMakeLists.txt" "${A2_DIR}/CMakeLists.txt"
else
  print_kv "ros2/A2/package.xml" "UNAVAILABLE"
fi
if [ "$REPO_ROOT" != "UNAVAILABLE" ]; then
  printf -- '- recommended build command: `cd %s/ros2 && colcon build --packages-select a2_lowlevel`\n' "$REPO_ROOT"
else
  printf -- '- recommended build command: `cd <AliengoSim2Real>/ros2 && colcon build --packages-select a2_lowlevel`\n'
fi

section "Notes"
cat <<'EOF'
- This report intentionally avoids dumping the full environment.
- Re-run with `--ping` only when the deploy machine is connected to the robot/network.
- Paste this Markdown report back to Codex when adjusting the A2 deployment chain.
EOF
