# 5. First boot from microSD

## 5.1 Flash and boot

1. Put the microSD card from step 4 in the phone.
2. Flash the boot image from step 3 (bootloader mode, slot A, `is-userspace: no`, check the SHA-256, `fastboot flash boot_a`, `fastboot reboot`; see [step 3](03-boot-image.md#flashing-for-later-steps)).

What happens (about 50 s to the Phosh lock screen):

1. LK shows its unlocked warning and loads `boot_a`; the shim starts Linux with our DTB.
2. `/init` (from the built-in initramfs) mounts the basics and sets up the **USB serial gadget** (`CMF-Mainline-Diagnostic-v9`) with a root shell on it.
3. It tries the internal root. Without `/cmf/area.bin` in the initramfs (you add it in step 6), that fails at once and it logs `internal unavailable; falling back to SD`.
4. It waits up to 15 s for the microSD, finds the partition with `/etc/cmf-native-boot`, and `switch_root`s into **systemd**.
5. systemd brings up Wi-Fi calibration, the modem, audio, Bluetooth and Phosh.

## 5.2 USB serial console

Plug the phone into your computer. It shows up as a USB modem (CDC ACM):

| Host | Device | Connect |
|---|---|---|
| Linux | `/dev/ttyACM0` | `picocom -b 115200 /dev/ttyACM0` |
| macOS | `/dev/cu.usbmodem*` | `screen /dev/cu.usbmodem* 115200` |
| Windows | `COMn` ("USB Serial Device", VID 1D6B) | PuTTY, serial, 115200 |

Before `switch_root` you get the initramfs BusyBox shell; afterwards `serial-getty@ttyGS0` logs you in as root automatically. For scripted use, `tools/serial/macser.py` (macOS/Linux, no pyserial needed) and `tools/serial/ser.py` (Windows, pyserial) send one command and print the reply.

The serial shell has no job control in the initramfs: never run a command that reads stdin there (add `</dev/null`).

## 5.3 First checks

```bash
phone# systemctl is-system-running        # running (or degraded: see which unit)
phone# systemctl --failed
phone# journalctl -b -u cmf-wifi-calibrate  # "NVRAM accepted", pre_cal summary, wlan0 up
phone# nmcli device wifi connect "<SSID>" password "<password>"
phone# ip -4 -br addr show wlan0
phone# mmcli -m 0 | grep -E 'state|access tech'   # connected / 5gnr or lte
phone# aplay -l                              # the CMF sound card
```

Then SSH in over Wi-Fi as your user, unlock Phosh with your password, and try a call.

## 5.4 If it doesn't come up

- **No serial device at all**: the kernel didn't start. Re-check the boot image's SHA-256, then flash your previous image or stock `boot_a.img`.
- **Serial works but it stays in the initramfs**: the SD wasn't found or has no `/etc/cmf-native-boot` or `/sbin/init`. Check `dmesg | grep -E 'cmf-init|native'` in the serial shell.
- **Emergency mode**: since `cmf-emergency-serial` is enabled, you still get a root shell on serial. Run `journalctl -xb` and look for the failed unit (often the root filesystem check).
- More in [8. Troubleshooting](08-troubleshooting.md).

Next: [6. Internal storage](06-internal-storage.md).
