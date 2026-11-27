# 2. Build the kernel

The kernel is [MT6878-Mainline/linux](https://github.com/MT6878-Mainline/linux) at commit `12f2e9f04` (Linux 7.2 development, 28 Aug 2026) plus the 14 patches in `kernel/patches/`:

| Patch | Area |
|---|---|
| 0001 | dt-bindings: MT6878 display, GCE, voter and power IDs |
| 0002 | clk: MT6878 display gates, voter, clock fixes |
| 0003 | pmdomain: display, modem and connectivity power domains |
| 0004 | memory: MT6878 SMI larbs |
| 0005 | SPMI PMIF, MT6363/MT6369 RF regulators, MT6685 audio regmap |
| 0006 | soc: mmsys/mutex, modem resources, GPUEB mailbox |
| 0007 | phy: MT6878 MIPI DSI PHY |
| 0008 | drm/mediatek: display pipeline, DSI panel, display IOMMU |
| 0009 | pinctrl EINT and MSDC fixes |
| 0010 | input: IRQ-driven MT6363 PMIC keys, FT5x06/FT3519 touch fixes |
| 0011 | power: CMF Phone 1 MT6375 battery fuel gauge |
| 0012 | net/wwan: MediaTek CCCI modem (data, voice) |
| 0013 | ASoC: MT6878 audio platform and MT6685 codec |
| 0014 | ASoC: aw88261: load the speaker tuning file late if missing at boot |

## 2.1 Firmware built into the Image

The config builds the GPU firmware into the kernel (`CONFIG_EXTRA_FIRMWARE`), and the speaker amplifier tuning too if you provide it. Put them under a `firmware/` directory:

```bash
host$ mkdir -p ~/cmf-build/firmware/arm/mali/arch11.8 ~/cmf-build/firmware/cmf
# GPU firmware, from upstream linux-firmware:
host$ curl -Lo ~/cmf-build/firmware/arm/mali/arch11.8/mali_csffw.bin \
      https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/arm/mali/arch11.8/mali_csffw.bin
# Optional: speaker amplifier tuning, from YOUR phone (step 1.4). Without it the
# amplifier driver loads /lib/firmware/cmf/aw882xx_acf.bin from the rootfs once
# it is mounted (patch 0014); the rest of the sound card works anyway.
host$ tar -xf backup/vendor-files/vendor-firmware.tar -O firmware/aw882xx_acf.bin \
      > ~/cmf-build/firmware/cmf/aw882xx_acf.bin
```

## 2.2 Build

```bash
host$ cd cmf-mainline
host$ BUSYBOX=/path/to/static-arm64-busybox scripts/build-kernel.sh ~/cmf-build
```

The script:
1. clones the base, checks out `12f2e9f04` and applies `kernel/patches/*.patch` with `git am`;
2. assembles the initramfs (`initramfs/init`, busybox, `cmf-dmarea`, the optional legacy `autostart.sh`, and `area.bin` if you pass it, see step 6);
3. builds `Image` with clang (`LLVM=1`, `LOCALVERSION=+`, release `7.2.0-cmf-wifi-pmic+`) and the DTB from `kernel/dts/mt6878-cmf-phone-1.dts`.

Output: `~/cmf-build/out/arch/arm64/boot/Image` and `~/cmf-build/out/cmf-phone-1.dtb`.

The first build takes 15–40 minutes. When you only change the initramfs (for example to add `area.bin` in step 6), run the script again; it only relinks.

## Notes

- **Kernel command line.** `CONFIG_CMDLINE_FORCE=y` ignores LK's arguments and uses `console=tty0 earlycon keep_bootcon clk_ignore_unused pd_ignore_unused regulator_ignore_unused panic=15 printk.devkmsg=on loglevel=8`. The `*_ignore_unused` flags keep the modem and display clocks and rails that LK left on.
- **Device tree.** `kernel/dts/` holds the device tree as a flat DTS (decompiled form, numeric phandles). `mt6878-cmf-phone-1-polled-keys.dts` is a fallback that polls the PMIC keys instead of using their IRQs.
- **Built-in drivers.** Display, GPU (Panthor), modem (CCCI), battery and keys are built in. The MediaTek connectivity stack (Wi-Fi/BT) and the Kinibi TEE driver are out-of-tree modules: see `drivers-oot/connectivity/` and `drivers-oot/`.
- **Building the out-of-tree modules.** Point them at the same `out/` directory: `make -C ~/cmf-build/linux O=~/cmf-build/out ARCH=arm64 LLVM=1 M=$PWD modules`.
- **Module signing.** The config enables module signing with a key generated at build time (`out/certs/signing_key.pem`). Out-of-tree modules must be built against the same `out/` tree so they get signed with it.

Next: [3. Boot image](03-boot-image.md).
