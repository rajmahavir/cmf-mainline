#!/bin/bash
# QEMU tests for initramfs/init: builds test kernels whose initramfs holds a
# fixture disk, and checks the internal-root paths (clean root, journal replay,
# a crash marker only in the journal, a marker on disk, and the old v11 init).
#
# usage: initramfs/tests/build-and-test.sh   (after scripts/build-kernel.sh)
#   env: d       work dir holding linux/ and out/ from build-kernel.sh (required)
#        s95a    dir whose initramfs.list is the production list (default: $d)
#        s94     dir holding an old init-v11 for the comparison test (optional)
#        qroot   root of a qemu-system-aarch64 + sgdisk install (default: /)
#        busybox static arm64 busybox (required)
# Before running: cp initramfs/init "$d/init-v12"; cp initramfs/tests/godown.c "$d/"
# (and an old init as "$s94/init-v11" if you want the comparison case).
set -euo pipefail
d=${d:?set d= to the build-kernel.sh work dir}
s95a=${s95a:-$d}
s94=${s94:-$d}
cd "$d"
qroot=${qroot:-/}
export LD_LIBRARY_PATH=$qroot/usr/lib/x86_64-linux-gnu
qemu=$qroot/usr/bin/qemu-system-aarch64
sgdisk=$qroot/usr/sbin/sgdisk
busybox=${busybox:?set busybox= to a static arm64 busybox}

stamp=$(cat build-timestamp 2>/dev/null || date -u '+%a %b %e %H:%M:%S UTC %Y' | tee build-timestamp)
# --- production build -------------------------------------------------------
if [ ! -e linux ]; then
    cp -a --reflink=auto "$s95a/linux" "$s95a/out" .
fi
sed "s|^file /init [^ ]*|file /init $d/init-v12 0755 0 0|" "$s95a/initramfs.list" > initramfs.list
grep -q "^file /init $d/init-v12 " initramfs.list
relink() {
    nice make -C linux O="$d/out" ARCH=arm64 LLVM=1 LOCALVERSION=+ \
        KBUILD_BUILD_VERSION=12 KBUILD_BUILD_TIMESTAMP="$stamp" \
        CONFIG_INITRAMFS_SOURCE="$1" -j16 Image
}
relink "$d/initramfs.list" > build-95c.log 2>&1
cmp "$s95a/out/.config" out/.config
test "$(cat out/include/config/kernel.release)" = '7.2.0-cmf-wifi-pmic+'
cp out/arch/arm64/boot/Image Image.production
sha256sum Image.production init-v12 initramfs.list

# --- fixtures ----------------------------------------------------------------
rm -rf fx && mkdir -p fx/root/{bin,sbin,etc,dev,proc,sys,lib/systemd,var/lib/cmf-native}
aarch64-linux-gnu-gcc -static -O2 -o fx/godown godown.c
install -m 0755 "$busybox" fx/root/bin/busybox
install -m 0755 "$busybox" fx/root/lib/systemd/systemd
: > fx/root/etc/cmf-native-boot
cat > fx/root/sbin/init <<'EOF'
#!/bin/busybox sh
k() { echo "<6>v12-root: $*" > /dev/kmsg; }
k "root=$(/bin/busybox grep ' / ' /proc/mounts)"
k "attempt=$(/bin/busybox cat /var/lib/cmf-native/attempt 2>&1)"
k "replayed=$(/bin/busybox cat /replayed 2>&1)"
k "V12-SWITCHROOT-OK"
/bin/busybox sleep 1
echo o > /proc/sysrq-trigger
/bin/busybox sleep 30
EOF
chmod 0755 fx/root/sbin/init
segs=8; seg=4096; stamp=8
fsk=$(( segs * (seg - stamp) / 2 ))          # KiB of mapped space
mke2fs -q -F -t ext4 -b 1024 -J size=1 -E root_owner=0:0 -d fx/root fx/fs.img "$fsk"
truncate -s 18M fx/ufs.img
"$sgdisk" --new=1:2048:$((2048 + segs * seg - 1)) --change-name=1:userdata fx/ufs.img
python3 - "$segs" <<'PY'
import hashlib, struct, sys, zlib
segs = int(sys.argv[1]); SEG = 4096; ST = 8
aid = bytes.fromhex("95c095c095c095c095c095c095c095c0")
sha = hashlib.sha256(b"Stage 95c QEMU fixture").digest()
starts = [i * SEG for i in range(segs)]
body = struct.pack("<8s16s32sQI", b"CMFTBL01", aid, sha, segs * SEG, segs)
body += b"".join(struct.pack("<Q", s) for s in starts)
open("fx/area.bin", "wb").write(body + struct.pack("<I", zlib.crc32(body)))
fs = open("fx/fs.img", "rb").read()
chunk = (SEG - ST) * 512
assert len(fs) == segs * chunk, (len(fs), segs * chunk)
with open("fx/ufs.img", "r+b") as disk:
    for i, s in enumerate(starts):
        st = struct.pack("<8s16sIQQ32s", b"CMFAREA1", aid, i, s, SEG, sha)
        st += struct.pack("<I", zlib.crc32(st))
        disk.seek((2048 + s) * 512)
        disk.write(st.ljust(ST * 512, b"\0"))
        disk.write(fs[i * chunk:(i + 1) * chunk])
PY
cat > fx/boot <<'EOF'
#!/bin/busybox sh
/bin/busybox mkdir -p /usr/bin /usr/sbin
/bin/busybox --install -s
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t devtmpfs devtmpfs /dev
cat /dev/kmsg > /dev/ttyAMA0 &
log() { echo "<6>v12-test: $*" > /dev/kmsg; }
sc=$(cat /scenario)
log "scenario $sc"
out=$(losetup -P /dev/loop0 /fake-ufs.img 2>&1) || log "losetup: $out"
i=0; while [ ! -b /dev/loop0p1 ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -b /dev/loop0p1 ] && log "fake userdata ready" || log "loop setup FAILED: $(ls /dev/loop* 2>&1 | tr '\n' ' ')"
if [ "$sc" != clean ]; then
    mkdir -p /prep
    /sbin/cmf-dmarea up /cmf/area.bin /dev/loop0p1
    mount -t ext4 /dev/mapper/cmf-root /prep
    case $sc in
        jmarker|dmarker) mkdir -p /prep/var/lib/cmf-native
                         echo "fixture-$sc" > /prep/var/lib/cmf-native/attempt ;;
        jdata) echo replayed-ok > /prep/replayed ;;
    esac
    # No sync before the shutdown: sync can checkpoint the change in place,
    # and the point is to leave it only in the journal (LOGFLUSH commits it).
    if [ "$sc" = dmarker ]; then
        sync
        umount /prep
    else
        /godown /prep && log "ext4 shutdown with log flush"
        # Snapshot the backing file now: the commit is in the journal, but the
        # dirty metadata is still only in the dm page cache (crash state).
        # umount would write it in place and hide what we want to test.
        cp /fake-ufs.img /crash-ufs.img
        umount /prep
    fi
    /sbin/cmf-dmarea down /cmf/area.bin
    if [ -e /crash-ufs.img ]; then
        losetup -d /dev/loop0
        losetup -P /dev/loop0 /crash-ufs.img
        i=0; while [ ! -b /dev/loop0p1 ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
        log "switched to crash snapshot"
    fi
    log "prep done"
fi
exec /stage-init
EOF
run() {
    name=$1 sc=$2 init=$3
    echo "$sc" > "fx/$name.scenario"
    grep -vE '^file (/init|/cmf/area.bin) ' initramfs.list > "fx/$name.list"
    cat >> "fx/$name.list" <<EOF
file /init $d/fx/boot 0755 0 0
file /stage-init $init 0755 0 0
file /scenario $d/fx/$name.scenario 0644 0 0
file /godown $d/fx/godown 0755 0 0
file /fake-ufs.img $d/fx/ufs.img 0644 0 0
file /cmf/area.bin $d/fx/area.bin 0644 0 0
EOF
    relink "$d/fx/$name.list" > "fx/$name.build.log" 2>&1
    cp out/arch/arm64/boot/Image "fx/Image.$name"
    log=$d/qemu-$name.log
    timeout 150 "$qemu" -L "$qroot/usr/share/qemu" -machine virt -cpu cortex-a76 \
        -m 768 -smp 2 -nodefaults -nic none -display none -monitor none \
        -serial stdio -no-reboot -kernel "fx/Image.$name" > "$log" 2>&1 &
    pid=$!
    for ((n = 0; n < 140; n++)); do
        grep -qE 'V12-SWITCHROOT-OK|falling back to SD|Kernel panic' "$log" && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    sleep 2; kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :
    echo "== $name"
    grep -E 'v12-test|v12-root|cmf-init|EXT4-fs \(dm-0\)|Kernel panic' "$log" | sed 's/^\[[^]]*\] //'
}
for t in ${TESTS:-clean:clean:v12 jmarker:jmarker:v12 jdata:jdata:v12 dmarker:dmarker:v12 v11-jmarker:jmarker:v11}; do
    IFS=: read -r name sc v <<< "$t"
    if [ "$v" = v11 ]; then run "$name" "$sc" "$s94/init-v11"; else run "$name" "$sc" "$d/init-v12"; fi
done

# Rebuild the production Image after the fixture relinks.
relink "$d/initramfs.list" > fx/restore.build.log 2>&1
# Relinks are not byte-reproducible here; record both hashes instead.
sha256sum Image.production out/arch/arm64/boot/Image
cp Image.production out/arch/arm64/boot/Image
echo STAGE95C_DONE
