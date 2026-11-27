#!/bin/busybox sh
set -eu
umask 077
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

work=/tmp/cmf-v13
header=/tmp/cmf-profile.header
archive=/tmp/cmf-profile.tar.gz
trap 'status=$?; rm -f "$work/wpa.conf" "$header" "$archive"; if [ "$status" -ne 0 ]; then echo "<3>CMF autostart failed with status $status"; fi' EXIT

partition=
for attempt in $(seq 1 30); do
    for event in /sys/class/block/*/uevent; do
        if grep -qx 'PARTNAME=boot_a' "$event" 2>/dev/null; then
            devname=$(sed -n 's/^DEVNAME=//p' "$event")
            partition="/dev/$devname"
            if [ ! -b "$partition" ]; then
                major=$(sed -n 's/^MAJOR=//p' "$event")
                minor=$(sed -n 's/^MINOR=//p' "$event")
                mknod "$partition" b "$major" "$minor" 2>/dev/null || true
            fi
            break 2
        fi
    done
    sleep 1
done
if [ ! -b "$partition" ]; then
    echo '<3>CMF autostart: boot_a block device unavailable'
    exit 1
fi

dd if="$partition" of="$header" bs=4096 skip=8192 count=1 2>/dev/null
{
    IFS= read -r marker
    IFS= read -r size
    IFS= read -r digest
} < "$header"
test "$marker" = CMF_BOOT_PROFILE_V1
case "$size" in ''|*[!0-9]*) exit 1 ;; esac
case "$digest" in *[!a-f0-9]*) exit 1 ;; esac
test "${#digest}" -eq 64
test "${#size}" -le 8
test "$size" -gt 0 && test "$size" -le 12582912
dd if="$partition" bs=4096 skip=8193 count="$(( (size + 4095) / 4096 ))" 2>/dev/null |
    head -c "$size" > "$archive"
printf '%s  %s\n' "$digest" "$archive" | sha256sum -c
mkdir -p "$work"
tar -xzf "$archive" -C "$work"
rm -f "$archive" "$header"
test -s "$work/wpa.conf"
test -s "$work/cmf_authorized_keys"
test -s "$work/cmf_dropbear_host_key"
test -x "$work/reboot-bootloader"
for step in prepare-unbound-fem calibrate power link ssh; do
    echo "<6>CMF autostart: $step"
    sh "$work/run-radio-test.sh" "$step"
done
echo "<6>CMF autostart: Wi-Fi and SSH ready"
