#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 OUTPUT_DIR" >&2
  exit 2
fi

output_dir="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
piper_root="$(cd "$script_dir/.." && pwd)"
image="a2-piper-pc2-bridge:humble-20260824"

if [ -e "$output_dir" ]; then
  echo "Output already exists: $output_dir" >&2
  exit 2
fi

install -d \
  "$output_dir/bridge" \
  "$output_dir/docker/image" \
  "$output_dir/packages" \
  "$output_dir/runtime" \
  "$output_dir/evidence"

PIPER_BRIDGE_PLATFORM=linux/amd64 \
PIPER_BRIDGE_IMAGE="$image" \
  bash "$piper_root/docker/build_image.sh"

docker image save \
  --output "$output_dir/docker/image/a2-piper-pc2-bridge_humble-20260824.tar" \
  "$image"

docker run --rm --interactive --platform linux/amd64 \
  -v "$output_dir/packages:/out" \
  ubuntu:22.04 bash -s <<'CONTAINER'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends ca-certificates curl
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
  -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
printf '%s\n' \
  'deb [arch=amd64 signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu jammy stable' \
  > /etc/apt/sources.list.d/docker.list
apt-get update
apt-get install -y --download-only \
  docker-ce \
  docker-ce-cli \
  containerd.io \
  docker-buildx-plugin \
  docker-compose-plugin \
  can-utils
cp /var/cache/apt/archives/*.deb /out/
CONTAINER

if ! compgen -G "$output_dir/packages/*.deb" >/dev/null; then
  echo "No offline packages were downloaded into $output_dir/packages" >&2
  exit 1
fi

cp "$piper_root/config/piper_bridge.yaml" "$output_dir/bridge/piper_bridge.yaml"
cp "$piper_root/pc2/compose.yaml" "$output_dir/docker/compose.yaml"
cp "$piper_root/pc2/piper_bridge.env" "$output_dir/docker/piper_bridge.env"
cp "$piper_root/pc2/install_offline.sh" "$output_dir/runtime/install_offline.sh"
cp "$piper_root/pc2/bridge_ctl.sh" "$output_dir/runtime/bridge_ctl.sh"
chmod +x "$output_dir/runtime/install_offline.sh" "$output_dir/runtime/bridge_ctl.sh"

docker image inspect "$image" \
  --format 'image={{index .RepoTags 0}} os={{.Os}} arch={{.Architecture}}' \
  > "$output_dir/docker/image/image-list.txt"
while IFS= read -r -d '' package_file; do
  package_name="$(dpkg-deb -f "$package_file" Package)"
  package_version="$(dpkg-deb -f "$package_file" Version)"
  package_arch="$(dpkg-deb -f "$package_file" Architecture)"
  printf '%s\t%s\t%s\n' "$package_name" "$package_version" "$package_arch"
done < <(
  find "$output_dir/packages" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z
) > "$output_dir/packages/package-list.txt"

echo "PASS: PC2 offline bundle prepared at $output_dir"
echo "The bundle does not configure CAN or start the bridge."
