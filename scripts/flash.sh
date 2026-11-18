#!/usr/bin/env bash
# Flash a patched system image and reboot.
#   scripts/flash.sh <image> [serial]
set -euo pipefail
[ $# -ge 1 ] || { echo "usage: $0 <image> [serial]" >&2; exit 2; }
img=$1
serial=${2:-${ANDROID_SERIAL:-}}
if [ -n "$serial" ]; then
	fastboot -s "$serial" flash system "$img"
	fastboot -s "$serial" reboot
else
	fastboot flash system "$img"
	fastboot reboot
fi
