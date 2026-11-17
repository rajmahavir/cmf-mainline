#!/bin/bash
set -eu
s=$(dirname "$0")
v=$HOME/cmfkali-v2-build
m=$v/host-tools/make-4.3/bin/make
export PATH="$v/host-tools/perl/usr/bin:$PATH"
export PERL5LIB="$v/host-tools/perl/usr/lib/x86_64-linux-gnu/perl-base"
w=$s/wlan/core/gen4m
extra="$s/connadp/Module.symvers $s/conninfra/Module.symvers $s/connfem/Module.symvers $s/wlan/adaptor/Module.symvers $s/wifi-kconfig.38gelAVP/modules-only.symvers"
log=$s/wlan-step$1.log
rc=0
"$m" -j4 -C "$s/linux" O="$s/kernel" ARCH=arm64 LLVM=1 M="$w" DEVICE_MODULES_PATH="$v/kernel_device_modules-6.1" DRIVER_BUILD_DATE=20261116155100 KCFLAGS=-Wno-error=missing-prototypes SEGMENT=SP MODULE_NAME=wlan_drv_gen4m_6878 CONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH=y CONFIG_MODIFY_TX_POWER_BY_BAT_VOLT=n TOP="$v" KBUILD_EXTRA_SYMBOLS="$extra" modules > "$log" 2>&1 || rc=$?
printf '\n=== WLAN compile exit=%s ===\n' "$rc"
rg -n '(^ +LD \[M\]|MODPOST|fatal error:|error:|ERROR:|WARNING: Module.symvers)' "$log" | cut -c 1-360 | head -55 || true
find "$w" -name '*.ko' -print
