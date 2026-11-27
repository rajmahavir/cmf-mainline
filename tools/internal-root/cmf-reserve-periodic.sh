#!/system/bin/sh
# Install beside cmf-reserve.sh in Magisk service.d.
# The existing script performs the initial boot check.
umask 077
if [ "${1:-}" != --locked ]; then
    /data/adb/magisk/busybox flock -n /data/adb/.cmf-reserve-periodic.lock \
        /system/bin/sh "$0" --locked </dev/null >/dev/null 2>&1 &
    exit 0
fi

while sleep 1800; do
    /system/bin/sh /data/adb/service.d/cmf-reserve.sh
done
