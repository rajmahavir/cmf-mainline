#!/usr/bin/env bash
set -euo pipefail

s=$HOME/mt6878-mainline/out/wifi-port-probe.Kl8geuUh
b=$s/rf-provider.zkcie89P
release=$(cat "$b/include/config/kernel.release")
export_dir=$(mktemp -d "$s/wifi-modules-v11.XXXXXXXX")
module_dir=$export_dir/lib/modules/$release
mkdir -p "$module_dir/extra"
sources=(
    "$b/crypto/cmac.ko"
    "$b/net/rfkill/rfkill.ko"
    "$b/net/wireless/cfg80211.ko"
    "$s/connadp/connadp.ko"
    "$s/connfem/connfem.ko"
    "$s/conninfra/conninfra.ko"
    "$s/wlan/adaptor/wmt_chrdev_wifi_connac2.ko"
    "$s/wlan/core/gen4m/wlan_drv_gen4m_6878.ko"
)
for src in "${sources[@]}"; do
    dest=$module_dir/extra/${src##*/}
    test -s "$src"
    llvm-strip --strip-debug -o "$dest" "$src"
    "$b/scripts/sign-file" sha512 "$b/certs/signing_key.pem" "$b/certs/signing_key.x509" "$dest"
    llvm-readelf -h "$dest" | rg -q "Machine:.*AArch64"
    vermagic=$(modinfo -F vermagic "$dest")
    test "${vermagic%% *}" = "$release"
    signer=$(modinfo -F signer "$dest")
    test -n "$signer"
    printf '%s: %s\n' "$(modinfo -F name "$dest")" "$(modinfo -F depends "$dest")"
done
cp "$b/modules.builtin" "$b/modules.builtin.modinfo" "$module_dir/"
: > "$module_dir/modules.order"
depmod -a -e -F "$b/System.map" -b "$export_dir" "$release" 2> "$export_dir/depmod.log"
test ! -s "$export_dir/depmod.log"
modprobe --dirname "$export_dir" --set-version "$release" --show-depends wlan_drv_gen4m_6878 \
    > "$export_dir/wlan-dependency-order.txt"
sed -i "s|$export_dir/||g" "$export_dir/wlan-dependency-order.txt"
(
    cd "$export_dir"
    sha256sum "lib/modules/$release/extra/"*.ko > SHA256SUMS
    sha256sum --check SHA256SUMS
)
printf '%s\n' "Kernel release: $release" "Architecture: AArch64" \
    "Built against the signed v11 diagnostic’s Module.symvers." \
    "The diagnostic keeps the connectivity, WLAN, and RF front-end DT nodes disabled." \
    "The modules passed depmod and have no on-device load test." \
    > "$export_dir/BUILD-INFO.txt"
archive=$s/mt6878-wifi-v11-modules.tar.gz
tar -C "$export_dir" -czf "$archive.tmp" BUILD-INFO.txt SHA256SUMS wlan-dependency-order.txt lib
mv "$archive.tmp" "$archive"
sha256sum "$archive"
