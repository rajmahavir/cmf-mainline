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
scan)
    trap 'status=$?; if [ "$status" -ne 0 ]; then dmesg | tail -n 160; fi' EXIT
    test -d /sys/class/net/wlan0
    ip link set wlan0 up
    tools/ld-musl-aarch64.so.1 --library-path tools tools/iw --version
    tools/ld-musl-aarch64.so.1 --library-path tools tools/iw reg get
    tools/ld-musl-aarch64.so.1 --library-path tools tools/iw dev wlan0 scan passive
    ;;
link)
    test -s wpa.conf
    ip link set wlan0 up
    tools/ld-musl-aarch64.so.1 --library-path tools tools/wpa_supplicant \
        -B -P /tmp/cmf-wpa.pid -D nl80211 -i wlan0 -c "$PWD/wpa.conf"
    for attempt in $(seq 1 25); do
        state=$(tools/ld-musl-aarch64.so.1 --library-path tools tools/wpa_cli \
            -p /tmp/cmf-wpa -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
        [ "$state" = COMPLETED ] && break
        sleep 1
    done
    printf 'WPA state: %s\n' "$state"
    if [ "$state" != COMPLETED ]; then
        dmesg | tail -n 40
        exit 1
    fi
    udhcpc -n -i wlan0 -t 5 -T 3 -p /tmp/cmf-udhcpc.pid -s "$PWD/run-radio-test.sh"
    ip -4 addr show dev wlan0
    gateway=$(ip -4 route show default dev wlan0 | awk '/^default via / {print $3; exit}')
    test -n "$gateway"
    ping -I wlan0 -c 3 -W 2 "$gateway"
    ;;
deconfig)
    test "${interface:-}" = wlan0
    ip -4 addr flush dev "$interface"
    ;;
bound|renew)
    test "${interface:-}" = wlan0
    ifconfig "$interface" "$ip" netmask "$subnet" up
    set -- ${router:-}
    if [ "$#" -gt 0 ]; then
        ip -4 route replace default via "$1" dev "$interface"
    fi
    ;;
ssh)
    umask 077
    test -s cmf_authorized_keys
    if ! grep -q '^root:' /etc/passwd 2>/dev/null; then
        mkdir -p /etc
        printf 'root:x:0:0:root:/root:/bin/sh\n' >> /etc/passwd
    fi
    root_home=$(awk -F: '$1 == "root" {print $6; exit}' /etc/passwd)
    test -n "$root_home"
    mkdir -p "$root_home/.ssh"
    chmod 700 "$root_home/.ssh"
    cp cmf_authorized_keys "$root_home/.ssh/authorized_keys"
    chmod 600 "$root_home/.ssh/authorized_keys"
    rm -f cmf_authorized_keys
    ipv4=$(ip -4 addr show dev wlan0 | awk '$1 == "inet" {split($2, a, "/"); print a[1]; exit}')
    test -n "$ipv4"
    host_key=/tmp/cmf-dropbear-ed25519
    if [ -s cmf_dropbear_host_key ]; then
        mv cmf_dropbear_host_key "$host_key"
        chmod 600 "$host_key"
        tools/ld-musl-aarch64.so.1 --library-path tools tools/dropbearkey \
            -y -f "$host_key" > /tmp/cmf-dropbear-hostkey.log
    else
        tools/ld-musl-aarch64.so.1 --library-path tools tools/dropbearkey \
            -t ed25519 -f "$host_key" > /tmp/cmf-dropbear-hostkey.log
    fi
    tools/ld-musl-aarch64.so.1 --library-path tools tools/dropbear \
        -s -j -k -E -P /tmp/cmf-dropbear.pid -r "$host_key" -p "$ipv4:2222" 2>/tmp/cmf-dropbear.log
    kill -0 "$(cat /tmp/cmf-dropbear.pid)"
    printf 'SSH_ADDRESS %s\n' "$ipv4"
    awk '$1 == "ssh-ed25519" {print "SSH_HOST_KEY", $1, $2}' /tmp/cmf-dropbear-hostkey.log
    ;;
*)
    echo "Usage: $0 prepare|prepare-unbound-fem|calibrate|power|scan|link|ssh" >&2
    exit 2
    ;;
esac
