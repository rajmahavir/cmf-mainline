# 8. Troubleshooting and crash logs

## First tools

- **USB serial console** (step 5.2): works from the initramfs onward, and in emergency mode.
- `journalctl -b`, `journalctl -b -1` (the previous boot), `systemctl --failed`.
- `dmesg | grep -E 'cmf-init|native'`: what `/init` decided.

## Off-device crash logging

RAM-based pstore (ramoops at `0x48090000`, 896 KiB, already in the DT) registers, but **nothing survives a reset on this phone**, not even a warm reboot: the bootloader clears or reuses that memory. So the phone streams its logs, as they are written, to another machine. After a hang or watchdog reset, the last lines are already off the device.

`tools/crashlog/`:
- **Phone:** `cmf-crashlog-kmsg.service` (`/dev/kmsg` → UDP 5514) and `cmf-crashlog-journal.service` (userspace journal → UDP 5515). Edit the receiver address in both units. The drop-in `wait-tailscale.conf` waits until the route to the receiver exists; if you use your LAN instead of Tailscale, change or remove it.
- **Receiver:** `receiver.py` + `cmf-phone-log.service` (a systemd user service; enable linger with `loginctl enable-linger <user>` so it survives logout and reboot). It writes `~/cmf-phone-log/phone-YYYYMMDD.log`, one line per message with the receive time.
- `journald.conf.d/cmf-sync.conf` (journal synced every 10 s) and `cmf-wlan-quiet.service` (Wi-Fi driver log level WARN/ERROR) keep the on-phone journal useful too.

## Known issues

| Symptom | Cause and fix |
|---|---|
| Emergency mode after a crash, "fsck failed" | Before init v12 the internal root ran without its journal (`noload` from the crash-guard probe carried over). Rebuild with the current `initramfs/init`; repair once from the microSD (step 7). |
| Occasional reset a few seconds after pressing Power to wake the screen | Under investigation (display power-on path or watchdog). The crash log captures the last lines; please report them. |
| No Wi-Fi after boot | `journalctl -b -u cmf-wifi-calibrate`. Usually missing firmware/NVRAM in `/lib/firmware` or modules not in `/usr/local/lib/cmf-wifi/<release>/`. The vendor probe runs only once per boot: reboot after fixing. |
| Wi-Fi driver floods the log | `cmf-wlan-quiet` sets `/proc/net/wlan/dbgLevel` to `0xff:0x03`. |
| No mobile data | Check the APNs in `/etc/default/cmf-modem` and the udev rule; `journalctl -b -u cmf-modem -u ModemManager`; `mmcli -m 0`. |
| Phosh doesn't start | `journalctl -b -u phosh`; check `/dev/dri/card0` (mediatek-drm) and `renderD128` (panthor) exist and that the Mesa path in the Phosh drop-in exists. |
| Suspend | s2idle works for a while and wakes on an incoming call, but it isn't finished (watchdog handling in suspend, Power-key wake). The sleep targets are masked by default. |
| Clock is wrong at boot | There is no RTC driver yet; `systemd-timesyncd` sets the time once the network is up. |

## Reporting problems

Include: the boot image SHA-256, the output of `journalctl -b -1 | tail -200` or the crash-log tail, `dmesg | grep cmf-init`, and what you did right before.
