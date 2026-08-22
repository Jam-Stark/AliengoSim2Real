#!/usr/bin/env bash
set -euo pipefail

sdk_root="${PIPER_SDK_ROOT:-}"
can_name="${PIPER_CAN_NAME:-can0}"
usb_address="${PIPER_USB_ADDRESS:-}"

if [ -z "$sdk_root" ]; then
  echo "Set PIPER_SDK_ROOT to the checked-out krushell/piper_sdk directory." >&2
  exit 2
fi
activate_script="$sdk_root/piper_sdk/can_activate.sh"
if [ ! -f "$activate_script" ]; then
  echo "Missing $activate_script" >&2
  exit 2
fi

args=("$can_name" "1000000")
if [ -n "$usb_address" ]; then
  args+=("$usb_address")
fi
bash "$activate_script" "${args[@]}"
ip -details link show "$can_name"
