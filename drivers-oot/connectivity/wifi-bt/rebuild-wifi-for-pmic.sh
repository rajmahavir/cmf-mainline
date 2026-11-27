#!/usr/bin/env bash
set -euo pipefail

scratch=$(dirname "$(realpath "$0")")
kernel_build=$(realpath "$1")
vendor=$HOME/cmf-vendor-bsp
kernel_src=$scratch/linux
make_host=$vendor/host-tools/make-4.3/bin/make
device_src=$vendor/kernel_device_modules-6.1
export PATH="$vendor/host-tools/perl/usr/bin:$PATH"
export PERL5LIB="$vendor/host-tools/perl/usr/lib/x86_64-linux-gnu/perl-base"

build_module() {
    local module_dir=$1
    shift
    "$make_host" -j4 -C "$kernel_src" O="$kernel_build" ARCH=arm64 LLVM=1 \
        M="$scratch/$module_dir" TOP="$vendor" DEVICE_MODULES_PATH="$device_src" \
        KCFLAGS=-Wno-error=missing-prototypes "$@" modules
}

test -s "$kernel_build/Module.symvers"
build_module connadp CONFIG_MTK_COMBO=m
build_module connfem
build_module conninfra CONFIG_MTK_COMBO_CHIP_CONSYS_6878=y \
    "KBUILD_EXTRA_SYMBOLS=$scratch/connadp/Module.symvers"
build_module wlan/adaptor CONNAC_VER=2_0 MODULE_NAME=wmt_chrdev_wifi_connac2 \
    "KBUILD_EXTRA_SYMBOLS=$scratch/connadp/Module.symvers $scratch/conninfra/Module.symvers"
build_module wlan/core/gen4m MODULE_NAME=wlan_drv_gen4m_6878 SEGMENT=SP \
    DRIVER_BUILD_DATE=20261116155100 CONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH=y \
    CONFIG_MODIFY_TX_POWER_BY_BAT_VOLT=n \
    "KBUILD_EXTRA_SYMBOLS=$scratch/connadp/Module.symvers $scratch/conninfra/Module.symvers $scratch/connfem/Module.symvers $scratch/wlan/adaptor/Module.symvers"
