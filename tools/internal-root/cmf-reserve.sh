#!/system/bin/sh
# Magisk service.d check for the Android reservation.
umask 077
dir=/data/cmf
image=$dir/linux.img
table=$dir/linux.extents
# The stock Android kernel exposes this attribute as an unsigned int.
maximum=4294967295

report() {
    message="$(date -u '+%Y-%m-%dT%H:%M:%SZ') $1"
    log -t cmf-reserve "$message"
    printf '%s\n' "$message" > "$dir/status.$$" &&
        mv -f "$dir/status.$$" "$dir/status"
}

alert() {
    report "MISMATCH $1"
    printf '%s\n' "$message" > "$dir/ALERT"
    cmd notification post -t 'CMF internal root' cmf-reserve "$message" >/dev/null 2>&1
    exit 1
}

# Resolve the F2FS instance mounted at /data in this namespace.
device=
while read -r id parent dev root mountpoint options tail; do
    [ "$mountpoint" = /data ] || continue
    case " $tail " in
        *' - f2fs '*) device=$dev ;;
    esac
done < /proc/self/mountinfo
[ -n "$device" ] || alert 'F2FS /data mount missing'
sysdev=$(readlink -f "/sys/dev/block/$device") || alert 'Cannot resolve /data device'
threshold="/sys/fs/f2fs/${sysdev##*/}/gc_pin_file_thresh"
before=$(cat "$threshold") || alert 'Cannot read GC threshold'
if [ "$before" != "$maximum" ]; then
    printf '%s\n' "$maximum" > "$threshold" || alert "GC threshold rejected, previously $before"
fi
after=$(cat "$threshold") || alert 'Cannot read back GC threshold'
[ "$after" = "$maximum" ] || alert "GC threshold read back as $after"

# check also verifies the F2FS pin and the immutable and compression flags.
"$dir/cmf-reserve" check "$image" "$table" || alert 'Pin, flags or extent table changed'
report "MATCH gc_pin_file_thresh=$before->$after" || exit 1
