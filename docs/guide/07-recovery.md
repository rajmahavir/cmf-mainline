# 7. Recovery, rollback and going back to Android

Every recovery path here only ever writes `boot_a`.

## Getting into the bootloader

| State | How |
|---|---|
| Linux running | `systemctl reboot --reboot-argument=bootloader` (fastboot appears in about a minute) |
| Android running | `adb reboot bootloader` |
| Hung or boot-looping | hold **Power ~10 s** until it restarts, then hold **Volume Up + Power** |

Always check before flashing:
```bash
host$ fastboot getvar current-slot    # a
host$ fastboot getvar is-userspace    # no
host$ sha256sum <image>               # matches the value you recorded
host$ fastboot flash boot_a <image> && fastboot reboot
```

## Roll back a bad kernel

Keep the last image that booted. Flash it with the steps above. Nothing else on the phone changes, so the root filesystems are unaffected.

## Go back to Android, and come back

Android and Linux share `boot_a`:
- **To Android:** flash your stock (or Magisk-patched) `boot_a.img` from step 1.3. Android boots normally with all its data; the reservation in `/data/cmf` stays in place, and the Magisk check reports `MATCH`.
- **Back to Linux:** flash your Linux `boot.img`. `/init` re-verifies the stamps and boots the internal root.

## Internal root won't boot

`/init` falls back to the microSD automatically when:
- a stamp is missing or wrong (Android moved or deleted the file, or a factory reset happened);
- the previous internal boot never reached `multi-user.target` (crash guard; the marker is `/var/lib/cmf-native/attempt`);
- the journaled mount fails, or the root is missing `/etc/cmf-native-boot` or systemd.

From the microSD system you can inspect and repair the internal root:
```bash
phone# U=/dev/disk/by-partlabel/userdata
phone# blockdev --setro $U && cmf-dmarea up area.bin $(readlink -f $U)    # read-only mapping
phone# mount -o ro,noload /dev/mapper/cmf-root /mnt                       # look, read logs
phone# journalctl -D /mnt/var/log/journal --list-boots
phone# umount /mnt && e2fsck -fn /dev/mapper/cmf-root                      # read-only check
```
To repair: `cmf-dmarea down area.bin`, `blockdev --setrw $U`, `cmf-dmarea up …` again (now writable), `e2fsck -fy /dev/mapper/cmf-root`, then `e2fsck -fn` must exit 0. Remove the crash marker `/var/lib/cmf-native/attempt` on the internal root, unmap, `blockdev --setro $U`, and reboot.

## Emergency mode

If systemd drops into emergency mode (usually a failed root `fsck`), `cmf-emergency-serial` keeps an autologin root shell on the USB serial port. Use `journalctl -xb`, fix the cause, then `systemctl default` or reboot. On a phone without a keyboard, this is the only way in.

## Hardware watchdog

`/etc/systemd/system.conf.d/cmf-wdt.conf` makes systemd feed the MediaTek watchdog (`RuntimeWatchdogSec=20s`). If userspace hangs, the phone resets within about 20 s instead of staying frozen. With the journaled root that reset is harmless.
