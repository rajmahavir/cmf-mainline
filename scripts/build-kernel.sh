#!/bin/bash
# Build the CMF Phone 1 kernel Image (with its built-in initramfs) from the
# MT6878-Mainline base plus this repository's patches.
#
# usage: scripts/build-kernel.sh WORKDIR [AREA_BIN]
#   WORKDIR   scratch directory (needs ~6 GB); reused on later runs
#   AREA_BIN  optional: your phone's internal-root area table (see
#             docs/guide/06-internal-storage.md). Without it the image boots
#             only from the microSD card.
#
# Needs: git, clang/lld/llvm (LLVM=1), make, flex, bison, bc, libssl-dev,
#        libelf-dev, an arm64 static busybox and aarch64-linux-gnu-gcc (for
#        cmf-dmarea). Tested on an x86-64 Ubuntu host.
#
# Built-in firmware (CONFIG_EXTRA_FIRMWARE) is read from $FIRMWARE_DIR
# (default WORKDIR/firmware):
#   arm/mali/arch11.8/mali_csffw.bin  from linux-firmware (GPU, Panthor)
#   cmf/aw882xx_acf.bin               optional, from YOUR phone's vendor
#                                     partition (speaker amplifier tuning; see
#                                     docs/guide/02-kernel.md). Without it the
#                                     driver loads /lib/firmware/cmf/
#                                     aw882xx_acf.bin from the rootfs once it
#                                     is mounted (patch 0014).
#
# KBUILD_BUILD_USER/HOST default to "cmf"/"cmf-mainline" so your user and host
# names don't end up in the Image.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:?usage: $0 WORKDIR [AREA_BIN]}
AREA=${2:-}
BASE_URL=https://github.com/MT6878-Mainline/linux.git
BASE_COMMIT=12f2e9f04
BUSYBOX=${BUSYBOX:?set BUSYBOX=/path/to/static-arm64-busybox (docs/guide/01-prepare.md)}

mkdir -p "$WORK"
cd "$WORK"

# 1. Kernel source: base commit + patch series
if [ ! -d linux/.git ]; then
    git clone --filter=blob:none "$BASE_URL" linux
fi
git -C linux checkout -q "$BASE_COMMIT"
git -C linux checkout -q -B cmf "$BASE_COMMIT"
git -C linux -c user.name=build -c user.email=build@localhost am -q --whitespace=nowarn "$REPO"/kernel/patches/*.patch

# 2. Initramfs inputs
mkdir -p initramfs
cp "$REPO/initramfs/init" initramfs/init
[ -x "$BUSYBOX" ] || { echo "set BUSYBOX= to a static arm64 busybox (e.g. from busybox-static:arm64)"; exit 1; }
if command -v file >/dev/null; then
    file "$BUSYBOX" | grep -q 'ARM aarch64.*statically linked' || { echo "$BUSYBOX is not a static arm64 binary"; exit 1; }
fi
cp "$BUSYBOX" initramfs/busybox
aarch64-linux-gnu-gcc -static -O2 -o initramfs/cmf-dmarea "$REPO/tools/internal-root/cmf-dmarea.c"
cat > initramfs.list <<EOF
dir /bin 0700 0 0
dir /cmf 0700 0 0
dir /proc 0700 0 0
dir /root 0700 0 0
dir /run 0700 0 0
dir /sbin 0700 0 0
dir /sys 0700 0 0
dir /tmp 0700 0 0
dir /usr 0700 0 0
dir /usr/bin 0700 0 0
dir /usr/sbin 0700 0 0
dir /dev 0755 0 0
dir /etc 0755 0 0
dir /etc/modprobe.d 0755 0 0
file /init $WORK/initramfs/init 0755 0 0
file /bin/busybox $WORK/initramfs/busybox 0755 0 0
slink /bin/sh busybox 0777 0 0
file /cmf/autostart.sh $REPO/initramfs/rootfs/cmf/autostart.sh 0755 0 0
file /etc/modprobe.d/gpu-first-boot.conf $REPO/initramfs/rootfs/etc/modprobe.d/gpu-first-boot.conf 0644 0 0
file /sbin/cmf-dmarea $WORK/initramfs/cmf-dmarea 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/null 0666 0 0 c 1 3
nod /dev/kmsg 0666 0 0 c 1 11
nod /dev/tty0 0666 0 0 c 4 0
nod /dev/tty1 0666 0 0 c 4 1
nod /dev/random 0666 0 0 c 1 8
nod /dev/urandom 0666 0 0 c 1 9
EOF
if [ -n "$AREA" ]; then
    echo "file /cmf/area.bin $(realpath "$AREA") 0644 0 0" >> initramfs.list
fi

# 3. Build
FIRMWARE_DIR=$(realpath "${FIRMWARE_DIR:-$WORK/firmware}")
FIRMWARE=arm/mali/arch11.8/mali_csffw.bin
[ -s "$FIRMWARE_DIR/$FIRMWARE" ] || { echo "missing firmware $FIRMWARE_DIR/$FIRMWARE (see docs/guide/02-kernel.md)"; exit 1; }
if [ -s "$FIRMWARE_DIR/cmf/aw882xx_acf.bin" ]; then
    FIRMWARE="$FIRMWARE cmf/aw882xx_acf.bin"
else
    echo "note: no cmf/aw882xx_acf.bin; the speaker needs it in the rootfs /lib/firmware"
fi
export KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-cmf} KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-cmf-mainline}
mkdir -p out
sed -e "s|^CONFIG_INITRAMFS_SOURCE=.*|CONFIG_INITRAMFS_SOURCE=\"$WORK/initramfs.list\"|" \
    -e "s|^CONFIG_EXTRA_FIRMWARE=.*|CONFIG_EXTRA_FIRMWARE=\"$FIRMWARE\"|" \
    -e "s|^CONFIG_EXTRA_FIRMWARE_DIR=.*|CONFIG_EXTRA_FIRMWARE_DIR=\"$FIRMWARE_DIR\"|" \
    "$REPO/kernel/config-7.2.0-cmf" > out/.config
make -C linux O="$WORK/out" ARCH=arm64 LLVM=1 LOCALVERSION=+ olddefconfig
make -C linux O="$WORK/out" ARCH=arm64 LLVM=1 LOCALVERSION=+ -j"$(nproc)" Image
DTC=${DTC:-$(command -v dtc || echo "$WORK/out/scripts/dtc/dtc")}
"$DTC" -q -I dts -O dtb -o out/cmf-phone-1.dtb "$REPO/kernel/dts/mt6878-cmf-phone-1.dts"

echo
echo "kernel release: $(cat out/include/config/kernel.release)"
sha256sum out/arch/arm64/boot/Image out/cmf-phone-1.dtb
echo "Next: scripts/build-boot-image.py (docs/guide/03-boot-image.md)"
