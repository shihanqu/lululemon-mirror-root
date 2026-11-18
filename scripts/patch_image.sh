#!/usr/bin/env bash
# Write build/mirror_root.ko into a system image at /lib/modules/adsprpc.ko.
#   scripts/patch_image.sh <system.img> <output.img>
# Input may be raw ext4 or Android sparse; output is sparse.
set -euo pipefail

if [ $# -ne 2 ]; then
	echo "usage: $0 <system.img> <output.img>" >&2
	exit 2
fi
root=$(cd "$(dirname "$0")/.." && pwd)
in=$1
out=$2
ko="$root/build/mirror_root.ko"

[ -f "$ko" ] || { echo "missing $ko (run scripts/build.sh first)" >&2; exit 1; }

# e2fsprogs is keg-only on Homebrew; find it without depending on the caller's PATH.
for d in /opt/homebrew/opt/e2fsprogs/sbin /usr/local/opt/e2fsprogs/sbin; do
	[ -d "$d" ] && PATH="$d:$PATH"
done
export PATH

for t in debugfs e2fsck python3; do
	command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# 1. normalise to raw ext4
magic=$(python3 - "$in" <<'EOF'
import sys
print(open(sys.argv[1], 'rb').read(4).hex())
EOF
)
if [ "$magic" = "3aff26ed" ]; then
	echo "input is Android sparse; converting"
	python3 "$root/tools/sparse2raw.py" "$in" "$work/raw.img"
else
	echo "input treated as raw ext4"
	cp "$in" "$work/raw.img"
fi

# 2. install the module
debugfs -w -R "rm /lib/modules/adsprpc.ko" "$work/raw.img" >/dev/null 2>&1 || true
debugfs -w -R "write $ko /lib/modules/adsprpc.ko" "$work/raw.img" >/dev/null
for f in "mode 0100644" "uid 0" "gid 0"; do
	debugfs -w -R "sif /lib/modules/adsprpc.ko $f" "$work/raw.img" >/dev/null
done

# 3. verify and re-encode
e2fsck -fy "$work/raw.img" >/dev/null
python3 "$root/tools/raw2sparse.py" "$work/raw.img" "$out"
echo "wrote $out"
echo "flash with: scripts/flash.sh $out"
