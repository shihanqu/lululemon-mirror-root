#!/usr/bin/env bash
# Build the kernel module with clang only (no kernel tree, no kbuild, no linker).
#   scripts/build.sh          -> build/mirror_root.ko
#   scripts/build.sh probe    -> build/mirror_probe.ko   (zero-import canary)
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/build"
mkdir -p "$out"

src="$root/mod/mirror_root.c"
if [ "${1:-}" = probe ]; then
	src="$root/mod/mirror_probe.c"
fi

ko="$out/$(basename "${src%.c}").ko"
clang --target=aarch64-linux-gnu -ffreestanding -fno-stack-protector -fno-pic \
	-fno-asynchronous-unwind-tables -fno-addrsig -O2 -c "$src" -o "$ko"

echo "built $ko ($(wc -c <"$ko" | tr -d ' ') bytes)"
echo "install path in the image: /lib/modules/adsprpc.ko"
