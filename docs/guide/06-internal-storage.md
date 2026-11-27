# 6. Move the root to internal storage

Running from microSD works, but UFS is much faster and the card can then stay in the phone as a rescue system. We don't repartition the phone and we never touch Android's encryption. Instead, **Android reserves a 25 GiB file in its own `/data`**, and Linux uses that file's physical blocks directly.

## How it works

- In Android, `cmf-reserve` creates `/data/cmf/linux.img`: **pinned** (F2FS never moves a pinned file), fully allocated, uncompressed and **immutable**. Its physical extents (FIEMAP) are therefore fixed. This is the same mechanism Android uses for DSU and Virtual A/B images.
- On F2FS with file-based encryption, those physical byte offsets map 1:1 onto the raw `userdata` partition. Linux joins the extents with **dm-linear** into `/dev/mapper/cmf-root` and puts its own ext4 there. Android never reads the file; Linux never writes outside it.
- The first 4 KiB of every 2 MiB segment holds a **stamp** (magic, area ID, index, position, table hash, CRC). If Android ever moves, deletes or resets the file, discard or reuse wipes the stamps, and Linux refuses the area and boots the microSD instead.
- At boot, `/init` verifies all stamps (read only), creates the mapping, and checks a **crash marker**: if the previous internal boot never reached `multi-user.target`, it boots the microSD instead. Since init v12 the root runs **with its ext4 journal**, so an unclean reset is replayed instead of needing a manual `fsck`.
- In Android, a Magisk `service.d` script re-checks the pin, the flags and the extent table at every boot and every 30 minutes, keeps F2FS's GC pin threshold at its maximum, and raises `/data/cmf/ALERT` if anything changed.

## 6.1 Reserve the file (Android, root)

Build the helper on the host and push it:
```bash
host$ aarch64-linux-gnu-gcc -static -O2 -o cmf-reserve tools/internal-root/cmf-reserve.c
host$ adb push cmf-reserve tools/internal-root/cmf-reserve*.sh /data/local/tmp/
```
Boot Android (flash your stock `boot_a.img` if Linux is on `boot_a`), then:
```bash
android# cd /data/local/tmp
android# ./cmf-reserve create /data/cmf/linux.img 26843545600      # 25 GiB; zero-fills, takes a few minutes
android# chattr +i /data/cmf/linux.img
android# cp cmf-reserve /data/cmf/ && chmod 0700 /data/cmf/cmf-reserve
android# /data/cmf/cmf-reserve map /data/cmf/linux.img > /data/cmf/linux.extents
android# /data/cmf/cmf-reserve check /data/cmf/linux.img /data/cmf/linux.extents   # MATCH
android# cp cmf-reserve.sh cmf-reserve-periodic.sh /data/adb/service.d/ && chmod 0755 /data/adb/service.d/cmf-reserve*.sh
host$ adb exec-out su -c 'cat /data/cmf/linux.extents' > linux.extents
```
Reboot Android once and check that `/data/cmf/status` says `MATCH`.

## 6.2 Build the area table and write the stamps (Linux, booted from microSD)

Flash your Linux boot image back (it still boots from the microSD) and work as root on the phone:
```bash
phone# U=/dev/disk/by-partlabel/userdata
phone# python3 cmf-area.py build linux.extents $(blockdev --getsz $U) area     # area/area.bin, area/area.json
phone# blockdev --setrw $U
phone# python3 cmf-area.py stamp area/area.bin $U           # stamps written=12800 verified_bad=0
phone# blockdev --setro $U
phone# python3 cmf-area.py verify area/area.bin $U          # stamps ok=12800 bad=0
```
Copy `area/area.bin` (and `area.json`) to the host and keep them: they belong to this reservation only.

## 6.3 Create the filesystem and copy the system

`cmf-dmarea` is the same helper the initramfs uses (`aarch64-linux-gnu-gcc -static -O2 -o cmf-dmarea tools/internal-root/cmf-dmarea.c` on the host, then copy it to the phone).
```bash
phone# blockdev --setrw $U
phone# ./cmf-dmarea up area/area.bin $(readlink -f $U)     # verifies stamps, creates /dev/mapper/cmf-root
phone# mkfs.ext4 -L cmf-root /dev/mapper/cmf-root
phone# mkdir -p /run/cmf-root && mount /dev/mapper/cmf-root /run/cmf-root
phone# systemd-run --wait -p StandardOutput=journal rsync -aHAXx --numeric-ids / /run/cmf-root/ \
         --exclude=/proc --exclude=/sys --exclude=/dev --exclude=/run --exclude=/tmp \
         --exclude=/mnt --exclude=/media --exclude=/lost+found --exclude=/var/lib/cmf-native/
phone# echo 'LABEL=cmf-root / ext4 defaults,noatime,errors=remount-ro 0 1' > /run/cmf-root/etc/fstab
phone# mkdir -p /run/cmf-root/var/lib/cmf-native /run/cmf-root/{proc,sys,dev,run,tmp,mnt,media}
phone# test -e /run/cmf-root/etc/cmf-native-boot && echo marker-ok
phone# umount /run/cmf-root && ./cmf-dmarea down area/area.bin && blockdev --setro $U
```
Give the internal root its own SSH host keys if you want to tell the two systems apart (`rm /etc/ssh/ssh_host_*; dpkg-reconfigure openssh-server` on first internal boot).

## 6.4 Build a kernel that knows the area, and boot it

```bash
host$ BUSYBOX=… scripts/build-kernel.sh ~/cmf-build area/area.bin
host$ scripts/build-boot-image.py --image ~/cmf-build/out/arch/arm64/boot/Image \
        --dtb ~/cmf-build/out/cmf-phone-1.dtb --key ~/cmf-avb.pem --aosp-tools ~/aosp --out ~/cmf-build/boot.img
```
Flash it. On boot, the serial console and `journalctl -b | grep cmf-init` show:
```
cmf-init: stamps ok=12800 bad=0 first_bad=none
cmf-init: internal root mounted with journal
cmf-init: internal area OK; switch_root to /dev/mapper/cmf-root
```
and `findmnt /` shows `/dev/mapper/cmf-root` **without** `norecovery`.

## Choosing the microSD instead

- Create `/etc/cmf-prefer-sd` on the microSD's root and reboot: `/init` boots the card even when the internal area is fine. Delete it and reboot to go back.
- Automatic fallback: a bad stamp, a missing marker or an unfinished previous internal boot all make `/init` boot the card.

## Before an Android factory reset or reflash

The internal Linux root lives inside `userdata`. A factory reset, a `userdata`/`super` reflash or deleting `/data/cmf` destroys it (Linux then simply falls back to the card). **Back up the internal root first**, from a microSD boot:
```bash
phone# tar --xattrs --acls --numeric-owner -C /run/cmf-root -cpf - . | zstd | ssh <host> 'cat > cmf-root.tar.zst'
```
To retire the reservation: remove the two scripts from `/data/adb/service.d/`, reboot Android, `chattr -i /data/cmf/linux.img`, then delete `/data/cmf`.

Next: [7. Recovery](07-recovery.md).
