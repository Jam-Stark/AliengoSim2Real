#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  ./scripts/bootstrap_policy_host_ubuntu.sh
  ./scripts/bootstrap_policy_host_ubuntu.sh --verify-only

Installs Docker Engine and Docker Compose v2 from Docker's official Ubuntu apt
repository, adds the invoking user to the docker group, and runs hello-world.
The install path uses sudo. Log out and back in once after the first install so
future Stage2 commands can use Docker without sudo.
USAGE
}

verify_only=0
case "${1:-}" in
  "") ;;
  --verify-only) verify_only=1 ;;
  -h|--help) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  usage >&2
  exit 2
fi

if [[ ! -r /etc/os-release ]]; then
  echo "ERROR: /etc/os-release is missing; this installer supports Ubuntu only." >&2
  exit 1
fi
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" ]]; then
  echo "ERROR: expected Ubuntu, found ID=${ID:-UNSET}." >&2
  exit 1
fi
if [[ "${VERSION_ID:-}" != "22.04" ]]; then
  echo "ERROR: expected Ubuntu 22.04, found VERSION_ID=${VERSION_ID:-UNSET}." >&2
  exit 1
fi
docker_arch="$(dpkg --print-architecture)"
if [[ "${docker_arch}" != "amd64" ]]; then
  echo "ERROR: expected amd64/x86_64, found ${docker_arch}." >&2
  exit 1
fi

if (( verify_only == 0 )); then
  sudo -v
  conflicting_packages=(
    docker.io docker-compose docker-compose-v2 docker-doc docker-buildx
    podman-docker containerd runc
  )
  installed_conflicts=()
  for package in "${conflicting_packages[@]}"; do
    if dpkg-query -W -f='${db:Status-Abbrev}' "${package}" 2>/dev/null \
        | grep -q '^ii '; then
      installed_conflicts+=("${package}")
    fi
  done
  if (( ${#installed_conflicts[@]} > 0 )); then
    echo "[INFO] removing packages that conflict with Docker's official packages: ${installed_conflicts[*]}"
    sudo apt-get remove -y "${installed_conflicts[@]}"
  fi
  sudo apt-get update
  sudo apt-get install -y ca-certificates curl
  sudo install -m 0755 -d /etc/apt/keyrings
  sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
    -o /etc/apt/keyrings/docker.asc
  sudo chmod a+r /etc/apt/keyrings/docker.asc

  docker_codename="${VERSION_CODENAME:?Ubuntu VERSION_CODENAME is missing}"
  printf '%s\n' \
    "Types: deb" \
    "URIs: https://download.docker.com/linux/ubuntu" \
    "Suites: ${docker_codename}" \
    "Components: stable" \
    "Architectures: ${docker_arch}" \
    "Signed-By: /etc/apt/keyrings/docker.asc" \
    | sudo tee /etc/apt/sources.list.d/docker.sources >/dev/null

  sudo apt-get update
  sudo apt-get install -y \
    docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

  login_user="${SUDO_USER:-${USER}}"
  sudo usermod -aG docker "${login_user}"
fi

docker --version
if docker compose version >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  docker compose version
  docker run --rm hello-world >/dev/null
  echo "[PASS] Docker Engine, Compose v2, daemon access, and hello-world are ready."
  exit 0
fi

if (( verify_only == 1 )); then
  echo "ERROR: Docker is installed but this login cannot use the daemon." >&2
  echo "Log out and back in, then rerun: $0 --verify-only" >&2
  exit 1
fi

sudo docker compose version
sudo docker run --rm hello-world >/dev/null
echo "[PASS] Docker Engine and Compose v2 are installed; sudo hello-world succeeded."
echo "REQUIRED ONCE: log out and back in so docker-group membership takes effect."
echo "Then verify without sudo: $0 --verify-only"
