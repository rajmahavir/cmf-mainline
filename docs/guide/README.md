# Build guide

This guide takes a stock Nothing CMF Phone 1 to Ubuntu + Phosh on mainline Linux, booting from internal storage, with Android still installed and restorable. We follow it ourselves.

| Step | You get | Time |
|---|---|---|
| [1. Prepare](01-prepare.md) | unlocked bootloader, full backups, rooted Android, a build host | 1–2 h |
| [2. Kernel](02-kernel.md) | `Image` with the built-in initramfs, and the DTB | 30–60 min |
| [3. Boot image](03-boot-image.md) | a signed `boot.img` for `boot_a` | 5 min |
| [4. Root filesystem](04-rootfs.md) | Ubuntu 26.04 + Phosh + CMF units on a microSD card | 1–2 h |
| [5. First boot](05-first-boot.md) | Linux running from microSD, USB serial shell, Wi-Fi, SSH | 30 min |
| [6. Internal storage](06-internal-storage.md) | the root moved into a reserved file in Android's `/data`; microSD becomes rescue only | 1–2 h |
| [7. Recovery](07-recovery.md) | rollback, SD fallback, Android round trip | reference |
| [8. Troubleshooting](08-troubleshooting.md) | serial console, crash logs, known issues | reference |

## Ground rules (they kept our phone alive through 95 stages)

1. **Back up first.** Step 1 backs up every partition you could ever need. Keep two copies, off the phone.
2. **Flash only `boot_a`.** This port never writes any other partition. `vbmeta`, `super`, `userdata`, the modem and calibration partitions stay stock.
3. **Keep a known-good image** for `boot_a` (the last one that booted) and the stock Android `boot_a` backup. Check SHA-256 before every flash.
4. **Keep the rescue microSD** working. If the internal root fails, `/init` falls back to it automatically.
5. **Use the USB serial console.** From the first second of boot the phone offers a root shell over USB (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS, a COM port on Windows). Most problems are visible there.

## Conventions

- `host$` is your build computer (x86-64 Ubuntu or Debian recommended; macOS works for flashing and serial only).
- `phone#` is a root shell on the phone running Linux (serial console or SSH).
- `android#` is a root shell in Android (`adb shell`, then `su`).
- Replace `<…>` placeholders with your own values.
