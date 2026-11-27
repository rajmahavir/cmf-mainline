#!/bin/sh
# Run in Phosh's Kali namespace after its system bus is available.
set -eu
modules=/opt/cmf-bluetooth
bridge=
bluez=
cleanup() {
    for pid in "$bluez" "$bridge"; do
        [ -n "$pid" ] || continue
        kill "$pid" 2>/dev/null || :
        wait "$pid" 2>/dev/null || :
    done
}
trap cleanup EXIT
trap 'exit 0' TERM HUP INT

[ -c /proc/1/root/dev/stpbt ] || exit 0
[ -d /sys/module/hci_vhci ] || /usr/sbin/insmod "$modules/hci_vhci.ko"
if [ ! -c /dev/vhci ]; then
    IFS=: read -r major minor < /sys/class/misc/vhci/dev
    mknod -m 0600 /dev/vhci c "$major" "$minor"
fi
if [ ! -e /sys/class/bluetooth/hci0 ]; then
    /usr/bin/python3 "$modules/cmf-bluetooth-hci.py" --stpbt /proc/1/root/dev/stpbt &
    bridge=$!
    for attempt in 1 2 3 4 5; do
        [ ! -e /sys/class/bluetooth/hci0 ] || break
        kill -0 "$bridge" 2>/dev/null || exit 1
        sleep 1
    done
    [ -e /sys/class/bluetooth/hci0 ] || exit 1
fi
# BlueZ must be able to subscribe to rfkill events before opening hci0.
if [ ! -c /dev/rfkill ]; then
    IFS=: read -r major minor < /sys/class/misc/rfkill/dev
    mknod -m 0600 /dev/rfkill c "$major" "$minor"
fi
if ! busctl --system status org.bluez >/dev/null 2>&1; then
    /usr/libexec/bluetooth/bluetoothd --nodetach &
    bluez=$!
fi
if [ -n "$bridge" ]; then wait "$bridge"; elif [ -n "$bluez" ]; then wait "$bluez"; fi
