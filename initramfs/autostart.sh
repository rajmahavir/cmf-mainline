#!/bin/sh
set -eu
umask 077

slot=/cmf/private-bundle.slot
work=/tmp/cmf-v13
trap 'status=$?; rm -f "$work/wpa.conf"; if [ "$status" -ne 0 ]; then echo "<3>CMF autostart failed with status $status"; fi' EXIT

read -r size < "$slot"
case "$size" in
    ??????????) ;;
    *) exit 1 ;;
esac
case "$size" in *[!0-9]*) exit 1 ;; esac
size=$(printf '%s' "$size" | sed 's/^0*//')
test -n "$size"
test "$size" -le "$(( $(wc -c < "$slot") - 11 ))"
mkdir -p "$work"
tail -c +12 "$slot" | head -c "$size" | tar -xzf - -C "$work"
test -s "$work/wpa.conf"
test -s "$work/cmf_authorized_keys"
test -s "$work/cmf_dropbear_host_key"
test -x "$work/reboot-bootloader"
for step in prepare-unbound-fem calibrate power link ssh; do
    echo "<6>CMF autostart: $step"
    sh "$work/run-radio-test.sh" "$step"
done
echo "<6>CMF autostart: Wi-Fi and SSH ready"
