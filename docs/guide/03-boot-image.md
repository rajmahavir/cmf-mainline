# 3. Build and sign the boot image

The stock LK bootloader loads `boot_a` (Android boot image header v4) and jumps to its gzip-compressed kernel payload. LK passes its own device tree, which a mainline kernel can't use, and `fastboot boot` is rejected on this phone, so every test is a flash of `boot_a`.

`scripts/build-boot-image.py` builds this payload:

| Offset | Content |
|---|---|
| `0x000000` | **shim header** (`boot/prebuilt/shim-head.bin`, 128 bytes): an arm64 Image header whose code loads `x0` with the embedded DTB and jumps to `+2 MiB` |
| `0x001000` | our **DTB** (must end before `0x10000`) |
| `0x010000` | **LK modem-record helper** (`boot/prebuilt/lk-modem.bin`, 10.6 KB; source in `boot/lk-modem-diag/`). It finds the modem record LK prepared and forwards it into our DTB (`/soc@0/mddriver@d180000`), so the CCCI driver can take over the modem LK already loaded. It also copies LK's `/memory` ranges into our DTB, so 6 GB and 8 GB phones both boot with their real RAM size; `/proc/device-tree/chosen/cmf,lk-memory` reads `lk` when that worked (otherwise the reason the built-in 8 GiB node was kept). The helper runs with the MMU off, so it is built with `-mstrict-align` |
| `0x200000` | the kernel **Image** (with its built-in initramfs) |

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

## Rebuilding the shim from source (optional)

`boot/fdt_shim.S` + `boot/wrapper.ld` are the shim header source; `boot/lk-modem-diag/` (`build-lk-modem-diag.sh`) builds the modem-record helper. If you rebuild them, compare against the SHA-256 values below before using your own:

| File | SHA-256 |
|---|---|
| `boot/prebuilt/shim-head.bin` | `b09eaf1b2655e956ac859edbebb59ac811e8dac1736b088a6b487d3a312fef6d` |
| `boot/prebuilt/lk-modem.bin` | `b2190d5442b5ef4d3239616308e10a9467f8a4e7e303842e490468a11859a729` |

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
