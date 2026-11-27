# Architecture: how the phone boots and runs

## 1. Bootloader → kernel

1. **Preloader and LK** (stock, unlocked) load `boot_a`, an Android boot image v4. LK prepares the modem (loads its image, reserves memory, writes a "modem record" into the device tree it hands over) and jumps to the gzip-decompressed kernel payload with **its own DTB** in `x0`.
2. Our payload starts with a **shim** (`boot/`): an arm64 Image header whose code replaces `x0` with **our embedded DTB** (at `+0x1000`) and jumps to the real kernel at `+2 MiB`. Before that, the helper at `+0x10000` (`boot/lk-modem-diag/`) copies LK's modem record and reservations into our DTB, so the mainline CCCI driver can adopt the modem LK already started.
3. The kernel ignores LK's command line (`CONFIG_CMDLINE_FORCE`) and keeps clocks, power domains and regulators that LK left on (`*_ignore_unused`).

## 2. Initramfs (`initramfs/init`, built into the Image)

| Step | What |
|---|---|
| Mounts | proc, sys, devtmpfs, devpts, configfs, pstore |
| USB serial | configures a CDC ACM gadget (`CMF-Mainline-Diagnostic-v9`) and a respawning root shell on `/dev/ttyGS0` |
| SD preference | if a microSD root holds `/etc/cmf-prefer-sd`, boot the SD first |
| Internal root | find the `userdata` partition; `cmf-dmarea verify` all stamps (read only); `cmf-dmarea up` → `/dev/mapper/cmf-root`; probe-mount `ro,noload`; check `/etc/cmf-native-boot` and systemd; refuse if the crash marker `/var/lib/cmf-native/attempt` exists; **remount with the journal** (replays it) and check the marker again; write a new marker; `switch_root` to systemd |
| microSD root | if the internal root is unavailable: first SD partition with `/etc/cmf-native-boot`, same marker logic, `switch_root` |
| Legacy | if neither works: run `/cmf/autostart.sh` (old boot-profile chain) and stay in the initramfs with the serial shell |

`cmf-native-ok.service` moves the marker to `last-ok` once `multi-user.target` is reached, so a boot that dies early makes the next boot fall back to the microSD.

## 3. Userspace (Ubuntu 26.04, systemd)

```
systemd
 ├─ cmf-wifi-calibrate ──▶ NetworkManager (Wi-Fi), bluetooth ──▶ cmf-bluetooth (HCI bridge)
 ├─ cmf-speechd (t2-speechd) ─┐
 ├─ cmf-modem (FSM start, fsd/rpcd, t2-modemd provisioning) ─▶ ModemManager (mtk-ccci) ─▶ cmf-cellular-profile
 ├─ cmf-audio-mixer
 ├─ cmf-kinibi (optional)
 ├─ serial-getty@ttyGS0, cmf-emergency-serial
 ├─ systemd-timesyncd (no RTC)
 └─ phosh.service (UID 1000; phoc on /dev/dri/card0 = mediatek-drm, rendering on renderD128 = panthor, Mesa 26.3)
```

### Why some things are unusual
- **Wi-Fi/BT modules live outside `/lib/modules`.** The vendor connectivity drivers probe once; if udev loaded them before their SPMI/PMIC suppliers were bound, Wi-Fi would stay dead until reboot. `cmf-wifi-calibrate` waits for the suppliers, loads the chain in order, writes the NVRAM, waits for calibration, then powers Wi-Fi on.
- **The modem is adopted, not booted.** LK loads and starts the modem; Linux's CCCI driver takes it over (`lk-owned` state on the first start). `cmf-modem-start` copies the NV partitions into RAM for the modem's file-system and RPC daemons, so Linux never writes the modem's calibration partitions.
- **Call audio** runs through `t2-speechd`, which bridges the modem's speech messages to the MT6878 audio platform.

## 4. Storage layout

| Where | What |
|---|---|
| `boot_a` | our signed boot image (stock Android image saved off-device) |
| `userdata` (F2FS, Android) | Android data **plus** `/data/cmf/linux.img`, a pinned, immutable 25 GiB file whose blocks Linux uses as `/dev/mapper/cmf-root` (ext4, journaled) |
| microSD | rescue Ubuntu root (same system) |
| everything else | untouched stock partitions |

Details: [guide step 6](guide/06-internal-storage.md).
