#!/bin/bash
set -euo pipefail
tools_dir=$(cd -- "$(dirname -- "$0")" && pwd)
base=${CMF_WORKSPACE:?Set CMF_WORKSPACE to the local mainline artifact directory}
stage=${CMF_CAPTURE_ASSETS:?Set CMF_CAPTURE_ASSETS to the local capture assets directory}
export CMF_SERIAL_PORT=${CMF_SERIAL_PORT:?Set CMF_SERIAL_PORT to the diagnostic USB serial path}
umask 077
logs=$(mktemp -d "$base/evidence/v14-startup.XXXXXX")
echo "Evidence directory: $logs"
serial=${CMF_DEVICE_SERIAL:?Set CMF_DEVICE_SERIAL to the phone serial from adb devices}
candidate=$base/images/diagnostic-v14-mt6685-boot.img
restore=$base/rollback/slot-a-current/boot_a.img
restore_hash=eda9f9a5e32dcaf478e8464a473cfb0d1e447c52d97b2e76d1036ff4d7b370fb
fast() { fastboot -s "$serial" "$@"; }
wait_fast() {
    local limit=$1
    for ((i=0; i<limit; i++)); do
        if fastboot devices | awk '{print $1}' | rg -qx "$serial"; then return 0; fi
        sleep 1
    done
    return 1
}
verify_fast() {
    [[ $(fast getvar current-slot 2>&1) == *"current-slot: a"* ]] &&
    [[ $(fast getvar is-userspace 2>&1) == *"is-userspace: no"* ]]
}
recover() {
    if ! wait_fast 2; then
        python3 "$tools_dir/diagnostic-console.py" reboot - 5 || true
        wait_fast 35 || return 1
    fi
    verify_fast && fast flash boot_a "$restore" && fast reboot || return 1
    for ((i=0; i<75; i++)); do
        if [[ $(adb -s "$serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
            if ! adb -s "$serial" shell 'su -c "dmesg"' > "$logs/android-restored-dmesg.txt"; then
                echo "Could not capture the post-restore Android kernel ring." >&2
            fi
            adb -s "$serial" shell 'getprop sys.boot_completed; getprop ro.boot.slot_suffix; uname -r'
            actual=$(adb -s "$serial" shell 'su -c "sha256sum /dev/block/by-name/boot_a"' | awk '{print $1}')
            [[ $actual == "$restore_hash" ]] || return 1
            echo "Android booted and boot_a read-back matches rollback SHA-256."
            return 0
        fi
        sleep 1
    done
    return 1
}
test -s "$stage/radio-v14.tar.gz"
test -s "$stage/reboot-bootloader"
[[ $(shasum -a 256 "$restore" | awk '{print $1}') == "$restore_hash" ]]
[[ $(shasum -a 256 "$candidate" | awk '{print $1}') == 6c02de9a05403e0675a131c44d4bb1b6b92b724441df93e7656ee88ebb80dff7 ]]
[[ $(adb -s "$serial" shell 'su -c "sha256sum /dev/block/by-name/boot_a"' | awk '{print $1}') == "$restore_hash" ]]
adb -s "$serial" shell 'su -c "dmesg"' > "$logs/android-dmesg.txt"
adb -s "$serial" reboot bootloader
wait_fast 40
verify_fast
trap 'status=$?; set +e; recover 2>&1 | tee "$logs/v14-mt6685-android-restore.log"; restore_status=$?; if ((restore_status != 0)); then exit 75; fi; exit "$status"' EXIT
{
    date '+%F %T %Z'
    fast getvar current-slot
    fast getvar is-userspace
    fast flash boot_a "$candidate"
    fast reboot
} 2>&1 | tee "$logs/v14-mt6685-fastboot.log"
python3 "$tools_dir/diagnostic-console.py" bootcheck - 55 > "$logs/v14-mt6685-console.log"
python3 "$tools_dir/diagnostic-console.py" upload "$stage/reboot-bootloader" 20 >> "$logs/v14-mt6685-console.log"
python3 "$tools_dir/diagnostic-console.py" run 'chmod 700 /tmp/cmf-v13/reboot-bootloader' 12 >> "$logs/v14-mt6685-console.log"
python3 "$tools_dir/diagnostic-console.py" upload "$stage/radio-v14.tar.gz" 110 >> "$logs/v14-mt6685-console.log"
python3 "$tools_dir/diagnostic-console.py" run 'cd /tmp/cmf-v13 && tar -xzf radio-v14.tar.gz && sh run-radio-test.sh prepare' 45 > "$logs/v14-mt6685-prepare.log"
python3 "$tools_dir/diagnostic-console.py" run \
    'cd /tmp/cmf-v13 && (cat /dev/kmsg > startup.kmsg </dev/null 2>startup-kmsg.err & echo $! > startup-kmsg.pid)' \
    15 > "$logs/mainline-capture-start.txt"
calibration_status=0
python3 "$tools_dir/diagnostic-console.py" run 'sh /tmp/cmf-v13/run-radio-test.sh calibrate' 75 > "$logs/v14-mt6685-calibration.log" || calibration_status=$?
python3 "$tools_dir/diagnostic-console.py" run \
    'cd /tmp/cmf-v13 && kill "$(cat startup-kmsg.pid)" && test ! -s startup-kmsg.err && cat startup.kmsg' \
    60 > "$logs/mainline-startup.kmsg"
exit "$calibration_status"
