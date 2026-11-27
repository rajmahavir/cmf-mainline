# 3. Build and sign the boot image

The stock LK bootloader loads `boot_a` (Android boot image header v4) and jumps to its gzip-compressed kernel payload. LK passes its own device tree, which a mainline kernel can't use, and `fastboot boot` is rejected on this phone, so every test is a flash of `boot_a`.

`scripts/build-boot-image.py` builds this payload:

| Offset | Content |
|---|---|
| `0x000000` | **shim header** (`boot/prebuilt/shim-head.bin`, 208 bytes): an arm64 Image header whose code calls the helper, loads `x0` with the embedded DTB and jumps to `+2 MiB`. With an Android kernel embedded (see below) it first runs the boot menu |
| `0x001000` | our **DTB** (must end before `0x10000`) |
| `0x010000` | **LK modem-record helper** (`boot/prebuilt/lk-modem.bin`, 14.4 KB, with the boot menu; source in `boot/lk-modem-diag/`). It finds the modem record LK prepared and forwards it into our DTB (`/soc@0/mddriver@d180000`), so the CCCI driver can take over the modem LK already loaded. It also copies LK's `/memory` ranges into our DTB, so 6 GB and 8 GB phones both boot with their real RAM size; `/proc/device-tree/chosen/cmf,lk-memory` reads `lk` when that worked (otherwise the reason the built-in 8 GiB node was kept). The helper runs with the MMU off, so it is built with `-mstrict-align` |
| `0x200000` | the kernel **Image** (with its built-in initramfs) |
| next 2 MiB boundary | optional: the **Android kernel** for dual boot |

It then runs AOSP `mkbootimg` (header v4) and `avbtool add_hash_footer` for the 64 MiB partition, signed with your key.

## Build

```bash
host$ scripts/build-boot-image.py \
        --image ~/cmf-build/out/arch/arm64/boot/Image \
        --dtb   ~/cmf-build/out/cmf-phone-1.dtb \
        --key   ~/cmf-avb.pem \
        --aosp-tools ~/aosp \
        --out   ~/cmf-build/boot.img
```

`--aosp-tools` is the directory holding `mkbootimg/mkbootimg.py` and `avb/avbtool.py` (step 1.5). The script prints the image's SHA-256; write it down.

We checked this builder against the image running on our phone: from the same Image and DTB it produces a byte-identical kernel payload.

## Optional: legacy rescue profile

`--profile profile.tar.gz` appends a "boot profile" at 32 MiB inside the image (header `CMF_BOOT_PROFILE_V1`, size, SHA-256). The old Kali-era chain used it to carry Wi-Fi modules, credentials and a dropbear SSH key, read by `initramfs/rootfs/cmf/autostart.sh`. Since the native systemd boot (Stage 91), **it is not needed**: `/init` only falls back to `autostart.sh` when neither the internal root nor the microSD root can boot. Leave it out unless you know you want it, and never publish a profile: it holds secrets.

## Optional: dual boot with Android

`--android-boot boot_a-android.img` embeds the kernel of an Android boot image (LZ4 or gzip, as Android ships it) after Linux. At every boot the shim then shows a **boot menu** over the logo:

```
        Start which system?
              Linux          <- highlighted (default)
             Android
           Linux in 3 s
        Volume Down: switch
```

**Volume Down** switches the highlighted entry and restarts the 3-second countdown. When it runs out, the highlighted system boots. Holding **Volume Down + Power** from power-on skips the menu and boots Android at once. Android gets LK's own device tree and the ramdisks LK already loaded from `init_boot_a`/`vendor_boot_a`, so it boots as from its own image.

The menu (`boot/lk-modem-diag/boot-menu.c`) runs before either kernel, with the MMU off. It draws into the framebuffer that LK's video-mode display pipeline is still scanning out: OVL0_2L layers 0 and 1, RGBA8888, 1080×2400. The font is the kernel's 8×16 console font. The timer is the arm64 generic counter. If the display registers don't look as expected, it shows nothing and only the Volume-Down-at-power-on rule applies.

- Use the Android boot image that matches your slot-a `init_boot_a`/`vendor_boot_a`, i.e. your own backup of `boot_a` from step 1. Restart Android normally to get back to Linux.
- Volume Down is GPIO 54 (active low, bit 22 of `0x10005210`); LK doesn't use that key. Volume Up and Power go through the PMIC, so the menu doesn't use them.
- It all has to fit LK's kernel area (`0x40000000`–`0x47c80000`, 124.5 MiB); the script checks. Linux + Android is about 70 MiB.
- Turn off automatic system updates in Android: an OTA installs to slot b and switches to it.
- `/proc/device-tree/chosen/cmf,boot-key-din` (under Linux) holds the raw register value the shim read.

## Rebuilding the shim from source (optional)

`boot/lk-modem-diag/build-lk-modem-diag.sh KERNEL_SOURCE OUT --control` builds both `shim-head.bin` (the entry code in `lk-modem-diag.S`) and `lk-modem.bin` (the helper). `boot/fdt_shim.S` is the older, helper-less shim, kept for reference. If you rebuild them, compare against the SHA-256 values below before using your own:

| File | SHA-256 |
|---|---|
| `boot/prebuilt/shim-head.bin` | `7eb5edc641d34f5f63d4dce5b90401e85ddbdd8b2b175b5e777c0b5eb54a3716` |
| `boot/prebuilt/lk-modem.bin` | `6f09bc40cbd8c9963147175838092981be5fff0b6b161df4ea97e4d2f7e3eef7` |

## Flashing (for later steps)

Bootloader mode: from Linux, `systemctl reboot --reboot-argument=bootloader`; from Android, `adb reboot bootloader`; with the phone off or hung, hold Power about 10 s, then Volume Up + Power. Then:

```bash
host$ fastboot getvar current-slot     # a
host$ fastboot getvar is-userspace     # no   (the real bootloader, not fastbootd)
host$ sha256sum ~/cmf-build/boot.img   # the value you wrote down
host$ fastboot flash boot_a ~/cmf-build/boot.img
host$ fastboot reboot
```

Never flash any other partition for this port.

Next: [4. Root filesystem](04-rootfs.md).
