# Hardware status

State on our reference phone (Stage 95c, 27 November 2026): Ubuntu 26.04 + Phosh 0.54, booted from the internal root. "Works" means it was used, not just probed.

| Component | Status | Notes |
|---|---|---|
| Boot | ✅ works | Stock LK (unlocked) → shim → Linux 7.2. Only `boot_a` is flashed. `fastboot boot` is not accepted by LK. |
| Dual boot with Android | ✅ works | optional: Volume Down + Power at power-on boots Android from the same `boot_a` ([guide step 3](guide/03-boot-image.md)) |
| USB serial console | ✅ works | CDC ACM gadget, root shell from the initramfs onward and in emergency mode |
| Display | ✅ works | 1080×2400 AMOLED, native `mediatek-drm` + DSI, display power cycling, backlight |
| GPU | ✅ works | Mali-G615 MC2, **Panthor** (built in, Mali CSF firmware built into the Image) + Mesa 26.3; Phosh renders with GLES2 |
| Touch | ✅ works | FocalTech FT3519 |
| Keys | ✅ works | Power + Volume Up via MT6363 PMIC IRQs; Volume Down via gpio-keys |
| Storage | ✅ works | UFS (all partitions visible); internal Linux root via dm-linear over a pinned file in `/data`; microSD |
| Wi-Fi | ✅ works | MediaTek gen4m (Wi-Fi 6), vendor modules out of tree, NVRAM from the phone |
| Bluetooth | ✅ works (basic) | vendor BT driver + HCI bridge; pairing lightly tested |
| Cellular data | ✅ works | 5G NR / LTE through CCCI + ModemManager (`mtk-ccci` plugin); tested on Reliance Jio |
| Voice calls | ✅ works | VoLTE (IMS) with call audio through `t2-speechd`; GNOME Calls |
| SMS | ⚠️ untested | ModemManager plugin has SMS support; not verified on this phone |
| Audio | ✅ works | speaker (AW88261 amp), microphones, call audio; headset jack not tested |
| Battery | ✅ works | MT6375 fuel gauge: capacity, charging status |
| Charging | ✅ works | charges while running Linux |
| Suspend (s2idle) | 🚧 in progress | real s2idle for >60 s and wake on incoming call verified; Power-key wake and watchdog handling in suspend still open; sleep targets masked by default |
| TEE (Kinibi) | ✅ optional | driver loads and talks to the TEE; nothing in the default system uses it yet |
| Hardware watchdog | ✅ works | fed by systemd (20 s) |
| RTC | ❌ missing | no DT node/driver; time comes from NTP |
| Camera | ❌ missing | |
| Sensors | ❌ missing | no accelerometer, light, proximity (no IIO devices) |
| Fingerprint | ❌ missing | |
| NFC | ❌ missing | |
| GPS/GNSS | ❌ missing | |
| Vibration | ❌ not tested | |
| USB OTG / host | ❌ not tested | the port runs in gadget mode for the serial console |

## Reference device

| | |
|---|---|
| Model | CMF Phone 1, A015 (`tetris`) |
| RAM / storage | 8 GB / 128 GB, SK hynix `H9QT0G6CN6X146` (RAM + UFS 2.2 package) |
| Display / touch | Samsung `s6e8fc3x02` AMOLED, FocalTech FT3519 |
| Bootloader | LK from an Android 16 Nothing OS build |

Other units may differ. The **6 GB** model (India only) should work: the boot shim copies the RAM layout LK reports into the device tree (tested in QEMU at 3, 6 and 8 GB; on the 8 GB phone it reads `lk`), but no 6 GB phone has run it yet. Phones with another display panel, touch controller or bootloader version are untested.

## Known problems

- **Rare reset a few seconds after a Power-key screen wake** (seen twice in about two days). Under investigation; stream the crash log ([guide step 8](guide/08-troubleshooting.md)) if you see it.
- RAM pstore doesn't survive resets on this phone (the bootloader clears it).
- Don't unbind and rebind the sound card (`/sys/bus/platform/drivers/mt6878-cmf`): the rebind oopses (CFI) in `snd_soc_bind_card` and the next reboot hangs. Reboot instead.
- The connectivity drivers are vendor code with a one-shot probe: if Wi-Fi calibration fails, reboot rather than reloading modules.
