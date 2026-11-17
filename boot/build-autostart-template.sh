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
if ! grep -Fq '"mediatek,mt6878-power-controller"' "$src/drivers/pmdomain/mediatek/mtk-pm-domains.c"; then
    echo 'Apply patches/mt6878-connectivity-power-domain.patch to the kernel source first.' >&2
    exit 1
fi
mkdir -p "$out"
build=$out/kernel
cp -a --reflink=auto "$baseline" "$build"
root="$out/root"
mkdir -p "$root"/{bin,sbin,usr/bin,usr/sbin,proc,sys,dev,tmp,run,root,cmf}
install -m 755 "$busybox" "$root/bin/busybox"
ln -sf busybox "$root/bin/sh"
install -m 755 "$assets/init" "$root/init"
install -m 755 "$assets/autostart.sh" "$root/cmf/autostart.sh"
cc -O2 "$src/usr/gen_init_cpio.c" -o "$build/usr/gen_init_cpio"
(
    cd "$build"
    "$src/usr/gen_initramfs.sh" -o "$out/initramfs.cpio" -u squash -g squash "$root" "$assets/dev_nodes.txt"
)
"$src/scripts/config" --file "$build/.config" \
    --set-str INITRAMFS_SOURCE "$out/initramfs.cpio" \
    --disable INITRAMFS_COMPRESSION_NONE --enable INITRAMFS_COMPRESSION_GZIP \
    --disable DEBUG_INFO \
    --disable DEBUG_INFO_DWARF5 \
    --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
    --disable DEBUG_INFO_BTF \
    --disable DEBUG_INFO_BTF_MODULES \
    --enable DEBUG_INFO_NONE
host_tools=/home/raj/cmfkali-v2-build/host-tools
if [[ -d "$host_tools/make-4.3/bin" ]]; then
    export PATH="$host_tools/perl/usr/bin:$host_tools/make-4.3/bin:$PATH"
    export PERL5LIB="$host_tools/perl/usr/lib/x86_64-linux-gnu/perl-base"
fi

lto_cache=""
if [[ -d "$baseline/lto-cache" ]]; then
    cp -a --reflink=auto "$baseline/lto-cache" "$out/thinlto-cache"
    lto_cache="$out/thinlto-cache"
elif [[ -d "$baseline/thinlto-cache" ]]; then
    cp -a --reflink=auto "$baseline/thinlto-cache" "$out/thinlto-cache"
    lto_cache="$out/thinlto-cache"
fi

make_cmd="make"
if [[ -x "$host_tools/make-4.3/bin/make" ]]; then
    make_cmd="$host_tools/make-4.3/bin/make"
fi

# Use single thread/job for ld.lld to avoid LLVM 21 threadpool context cleanup crashes
ld_flags="ld.lld --no-mmap-output-file --threads=1 --thinlto-jobs=1"
if [[ -n "$lto_cache" ]]; then
    ld_flags="$ld_flags --thinlto-cache-dir=$lto_cache"
fi

make_args=(-C "$src" "O=$build" ARCH=arm64 LLVM=1 "LD=$ld_flags" LDFLAGS_vmlinux=--strip-debug)
if [[ -x "$host_tools/perl/usr/bin/perl" ]]; then
    make_args+=("PERL=$host_tools/perl/usr/bin/perl")
fi

"$make_cmd" "${make_args[@]}" olddefconfig
grep -qx 'CONFIG_INITRAMFS_COMPRESSION_GZIP=y' "$build/.config"
"$make_cmd" -j"${CMF_JOBS:-4}" "${make_args[@]}" Image
cp "$build/arch/arm64/boot/Image" "$out/Image"
cp "$dtb" "$out/candidate.dtb"
