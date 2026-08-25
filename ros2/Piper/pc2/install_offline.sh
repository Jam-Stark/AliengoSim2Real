#!/usr/bin/env bash
set -euo pipefail

workspace_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="a2-piper-pc2-bridge:humble-20260824"
image_archive="$workspace_root/docker/image/a2-piper-pc2-bridge_humble-20260824.tar"

if [ "$(id -un)" != "unitree" ]; then
  echo "Run this script as unitree on A2 PC2." >&2
  exit 2
fi

sudo dpkg --install \
  "$workspace_root"/packages/containerd.io_*.deb \
  "$workspace_root"/packages/docker-ce-cli_*.deb \
  "$workspace_root"/packages/docker-ce_*.deb \
  "$workspace_root"/packages/docker-buildx-plugin_*.deb \
  "$workspace_root"/packages/docker-compose-plugin_*.deb \
  "$workspace_root"/packages/can-utils_*.deb
sudo systemctl enable --now docker
sudo usermod -aG docker unitree
sudo docker image load --input "$image_archive"
sudo docker image inspect "$image" --format 'image={{index .RepoTags 0}} os={{.Os}} arch={{.Architecture}}'

echo "PASS: Docker, Compose, can-utils, and the PiPER bridge image are installed."
echo "Log out and back in before running Docker without sudo."
echo "CAN was not configured and the bridge was not started."
