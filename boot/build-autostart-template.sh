#!/usr/bin/env bash
# Build an unpersonalized Linux Image using the known v15 kernel build.
set -euo pipefail
umask 077

if [[ $# != 5 ]]; then
    echo "Usage: $0 LINUX_SOURCE V15_BUILD BUSYBOX_V9 V15_DTB OUTPUT_DIRECTORY" >&2
    exit 2
fi
src=$(realpath "$1")
baseline=$(realpath "$2")
busybox=$(realpath "$3")
dtb=$(realpath "$4")
out=$(realpath -m "$5")
assets=$(cd -- "$(dirname -- "$0")/../initramfs" && pwd)
mkdir "$out"
build=$out/kernel
cp -a --reflink=auto "$baseline" "$build"
root="$out/root"
mkdir -p "$root"/{bin,proc,sys,dev,tmp,run,root,cmf}
install -m 755 "$busybox" "$root/bin/busybox"
install -m 755 "$assets/init" "$root/init"
install -m 755 "$assets/autostart.sh" "$root/cmf/autostart.sh"
python3 - "$root/cmf/private-bundle.slot" <<'PY'
from pathlib import Path
import sys

marker = b"CMF-V16-PRIVATE-BUNDLE-PLACEHOLDER\n"
Path(sys.argv[1]).write_bytes(marker.ljust(12 * 1024 * 1024, b"\0"))
PY
cc -O2 "$src/usr/gen_init_cpio.c" -o "$build/usr/gen_init_cpio"
(
    cd "$build"
    "$src/usr/gen_initramfs.sh" -o "$out/initramfs.cpio" -u squash -g squash "$root" "$assets/dev_nodes.txt"
)
"$src/scripts/config" --file "$build/.config" \
    --set-str INITRAMFS_SOURCE "$out/initramfs.cpio" \
    --disable INITRAMFS_COMPRESSION_GZIP --enable INITRAMFS_COMPRESSION_NONE
make_args=(-C "$src" "O=$build" ARCH=arm64 LLVM=1)
make "${make_args[@]}" olddefconfig
grep -qx 'CONFIG_INITRAMFS_COMPRESSION_NONE=y' "$build/.config"
make -j"${CMF_JOBS:-2}" "${make_args[@]}" Image
cp "$build/arch/arm64/boot/Image" "$out/Image"
cp "$dtb" "$out/candidate.dtb"
