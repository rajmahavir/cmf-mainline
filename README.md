# Mainline Linux on the Nothing CMF Phone 1

A working **mainline Linux 7.2** port for the **Nothing CMF Phone 1** (MediaTek Dimensity 7300 / MT6878, codename `tetris`). It runs **Ubuntu 26.04 with the Phosh mobile shell** as a daily-usable phone: GPU-accelerated display and touch, Wi-Fi, Bluetooth, **5G data and voice calls**, audio and battery. It boots from the phone's internal storage alongside Android, with a microSD card as the rescue system.

The stock bootloader (LK) is **not modified**. You unlock it, flash one partition (`boot_a`), and can go back to Android at any time.

> **Status: works for developers and tinkerers.** It has been used day to day on one device. Read [docs/status.md](docs/status.md) for exactly what works, and make the backups in [the guide](docs/guide/README.md) before you flash anything.

## What works

| | |
|---|---|
| Display | 1080×2400 AMOLED through native `mediatek-drm` (DSI), backlight |
| GPU | Mali-G615 MC2 through **Panthor** + Mesa (Phosh is GPU-rendered) |
| Touch, keys | FocalTech FT3519 touch; Power and Volume keys (IRQ driven) |
| Wi-Fi, Bluetooth | MediaTek connectivity stack (gen4m Wi-Fi 6, BT 5.3) as out-of-tree modules |
| Cellular | 5G/4G data through the CCCI modem driver and ModemManager; **VoLTE calls** (IMS) with call audio |
| Audio | Speaker, microphones, call audio |
| Power | Battery fuel gauge and charging status; suspend (s2idle) is under test |
| Storage | UFS; Linux root in a reserved file inside Android's `/data`, plus microSD |
| Debug | USB serial root console (`ttyGS0`) from the very first boot |

Missing: camera, sensors (accelerometer, proximity, light), fingerprint, NFC, RTC, GPS. See [docs/status.md](docs/status.md).

## How it fits together

```
 LK (stock, unlocked) ──loads boot_a──▶ shim ──▶ Linux 7.2 Image (+ built-in initramfs)
                                                   │  /init: USB serial shell, crash guard
                                                   ├─▶ internal root  (dm-linear over a pinned
                                                   │                   file in Android /data)
                                                   └─▶ microSD root   (rescue / first boot)
                                                         │
                                                         ▼
                                         systemd ─▶ Ubuntu 26.04 + Phosh + CMF units
```

[docs/architecture.md](docs/architecture.md) explains each stage.

## Build it yourself

Everything here is built from source. No prebuilt images and no vendor firmware are distributed; the few vendor files the phone needs are copied from **your own phone** during the guide.

1. [Prerequisites, unlocking and backups](docs/guide/01-prepare.md)
2. [Build the kernel](docs/guide/02-kernel.md)
3. [Build and sign the boot image](docs/guide/03-boot-image.md)
4. [Build the Ubuntu + Phosh root filesystem](docs/guide/04-rootfs.md)
5. [First boot from microSD](docs/guide/05-first-boot.md)
6. [Move the root to internal storage](docs/guide/06-internal-storage.md)
7. [Recovery, rollback and going back to Android](docs/guide/07-recovery.md)
8. [Troubleshooting and crash logs](docs/guide/08-troubleshooting.md)

Start at [docs/guide/README.md](docs/guide/README.md).

## Repository layout

| Path | What |
|---|---|
| `kernel/patches/` | 14 patches against [MT6878-Mainline/linux](https://github.com/MT6878-Mainline/linux) `12f2e9f04` |
| `kernel/config-7.2.0-cmf`, `kernel/dts/` | kernel config and the device tree |
| `initramfs/` | `/init` (USB serial shell, internal-root crash guard, journaled root) and its QEMU tests |
| `boot/` | the boot shim and LK modem-record helper (source + tiny prebuilt) |
| `scripts/` | `build-kernel.sh`, `build-boot-image.py` |
| `rootfs/` | rootfs build script, package list and the CMF systemd units/scripts (`overlay/`) |
| `drivers-oot/` | out-of-tree drivers: MediaTek connectivity patches (Wi-Fi/BT), display IOMMU, Kinibi TEE, test modules |
| `tools/` | internal-root reservation tools, serial helpers, off-device crash log |
| `docs/` | the guide, hardware status, architecture and project history |

The MediaTek connectivity drivers (Wi-Fi/BT) are carried as **patches against Nothing's published GPL kernel-module release** (`kernel_device_modules-6.1`), with a fingerprint of the exact source tree in `drivers-oot/connectivity/PROVENANCE.md`; the vendor sources themselves are not redistributed.

## Credits

This port stands on other people's work. See [NOTICE.md](NOTICE.md) for the full list, in particular:
- **[MT6878-Mainline](https://github.com/MT6878-Mainline)**: the mainline MT6878 kernel base, lplk and u-boot work.
- **Serlk's Titan 2 port** ([titan2-linux](https://github.com/Serlk/titan2-linux), [titan2-tools](https://github.com/Serlk/titan2-tools), [titan2-docs](https://github.com/Serlk/titan2-docs)): the CCCI modem and audio drivers, the ModemManager `mtk-ccci` plugin and the `t2-*` modem helpers, written for the Unihertz Titan 2, which uses the same SoC.
- **MediaTek**: vendor drivers carried in the patches and the connectivity repository (GPL).
- postmarketOS, Phosh, Ubuntu and the upstream Linux community.

## License

**GPL-2.0-only** for everything written for this project ([LICENSE](LICENSE)). Vendor and third-party code inside the patches keeps its own copyright headers. See [NOTICE.md](NOTICE.md).

## Disclaimer

Unlocking the bootloader and flashing images can lose data or brick a device. You do this at your own risk. This project is not affiliated with Nothing Technology, MediaTek or Unihertz.
