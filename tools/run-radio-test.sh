#!/bin/sh
# Run in the diagnostic RAM filesystem from the extracted module bundle.
set -eu

cd "$(dirname "$0")"
modules=lib/modules/$(uname -r)/extra

load_module() {
    local name=$1
    shift
    printf 'Loading %s\n' "$name"
    insmod "$modules/$name.ko" "$@"
}

wait_for_suppliers() {
    # The vendor one-shot probe cannot retry while kernel suppliers are deferred.
    for attempt in $(seq 1 25); do
        read -r uptime idle < /proc/uptime
        if [ "${uptime%.*}" -ge 15 ] &&
           [ -L /sys/bus/spmi/devices/0-04/driver ] &&
           [ -L /sys/bus/spmi/devices/0-05/driver ]; then
            return 0
        fi
        sleep 1
    done
    echo 'Timed out waiting for the RF PMIC providers' >&2
    return 1
}

check_clock=false
if [ -f "$modules/mt6685-consys-regmap.ko" ]; then
    check_clock=true
fi

case "${1:-}" in
prepare|prepare-unbound-fem)
    trap 'status=$?; if [ "$status" -ne 0 ]; then dmesg | tail -n 160; fi' EXIT
    sha256sum -c SHA256SUMS
    wait_for_suppliers
    mkdir -p /lib/firmware
    cp firmware/* /lib/firmware/
    if "$check_clock"; then
        load_module mt6685-consys-regmap
        test -L /sys/bus/spmi/devices/0-09/driver
        dmesg | grep -F 'MT6685 DCXO strap bit:'
    fi
    for module in cmac rfkill cfg80211 connadp connfem conninfra wmt_chrdev_wifi_connac2; do
        load_module "$module"
    done
    if [ "$1" = prepare-unbound-fem ]; then
        test -d /sys/module/connfem
        test ! -L /sys/bus/platform/devices/connfem/driver
        echo 'ConnFEM module loaded with no bound FEM provider'
    else
        test -L /sys/bus/platform/devices/connfem/driver
    fi
    test -L /sys/bus/platform/devices/18000000.consys/driver
    test -c /dev/wmtWifi
    if "$check_clock"; then
        test -L /sys/bus/platform/devices/*mt6685-consys/driver
    fi
    echo 'Connectivity providers bound and /dev/wmtWifi available'
    ;;
calibrate)
    trap 'status=$?; if [ "$status" -ne 0 ]; then dmesg | tail -n 160; fi' EXIT
    test -c /dev/wmtWifi
    test "$(wc -c < firmware/cmf-wifi-nvram.bin)" -eq 6146
    load_module wlan_drv_gen4m_6878 nvram=cmf-wifi-nvram.bin
    echo 'Submitting factory Wi-Fi NVRAM'
    request=$(mktemp)
    { printf 'WR-BUF:NVRAM'; cat firmware/cmf-wifi-nvram.bin; } > "$request"
    if dd if="$request" of=/dev/wmtWifi bs=8192 count=1; then
        rm -f "$request"
    else
        rm -f "$request"
        exit 1
    fi
    echo 'NVRAM accepted'
    load_module bluetooth
    load_module bt_drv_6878
    echo 'Waiting for Bluetooth and Wi-Fi pre-calibration'
    for attempt in $(seq 1 30); do
        if dmesg | grep -Fq '[pre_cal] summary'; then
            break
        fi
        sleep 1
    done
    dmesg | grep -E '\[pre_cal\] (driver|summary)'
    # The vendor log can print "success" alongside a negative return code.
    dmesg | grep -Eq '\[pre_cal\] summary .*bt_cal=\[[02]\]\[[0-9]+\] wf_cal=\[[02]\]'
    ;;
power)
    trap 'status=$?; if [ "$status" -ne 0 ]; then dmesg | tail -n 160; fi' EXIT
    printf '1' > /dev/wmtWifi
    ls /sys/class/ieee80211
    ls /sys/class/net
    test -d /sys/class/net/wlan0
    ;;
*)
    echo "Usage: $0 prepare|prepare-unbound-fem|calibrate|power" >&2
    exit 2
    ;;
esac
