# 1. Prepare: unlock, back up, root Android, set up a build host

## What you need

- Nothing CMF Phone 1 (A015, MT6878). Tested on one 8 GB RAM / 128 GB unit (used in India). The 6 GB and 256 GB models are untested; see [status](../status.md#reference-device).
- A microSD card, 32 GB or more (A1/A2, a known brand: it holds the rescue system).
- A USB-C data cable.
- A build host: x86-64 Linux (Ubuntu 24.04+ or Debian 13 recommended), ~40 GB free.
- `adb` and `fastboot` (Android platform-tools) on the computer you flash from.

## 1.1 Unlock the bootloader

Unlocking **erases the phone**. Copy off anything you want to keep first.

1. In Android: Settings → About phone → tap *Build number* seven times. Then Settings → System → Developer options → enable **OEM unlocking** and **USB debugging**.
2. Reboot to the bootloader and unlock:
   ```bash
   adb reboot bootloader
   fastboot flashing unlock      # confirm on the phone with the volume keys
   ```
3. Let Android boot and set it up again. The bootloader now shows an "unlocked/orange state" warning at each boot. That is expected: it is what allows a self-signed `boot_a`.

Check the slot. This port uses **slot A**:
```bash
fastboot getvar current-slot     # must print: a
```
If it prints `b`, set it with `fastboot --set-active=a` and make sure Android still boots before going on.

## 1.2 Root Android (Magisk)

Rooted Android is needed to back up partitions and, in step 6, to reserve the Linux root inside `/data`.

1. Install the Magisk app, extract `init_boot` from your firmware (or read it back with `dd` later), patch it in Magisk and flash it to `init_boot_a`. Follow the Magisk documentation for Android 14+ devices.
2. Confirm: `adb shell su -c id` prints `uid=0(root)`.

(Magisk modifies `init_boot`, not `boot`, so it keeps working alongside this port.)

## 1.3 Full backup

From rooted Android, dump every partition by name to the computer. This is your insurance; the calibration partitions (`nvdata`, `nvcfg`, `protect1/2`, `proinfo`) are unique to your phone and cannot be downloaded anywhere.

```bash
host$ mkdir -p backup && cd backup
host$ adb shell su -c 'ls /dev/block/by-name' | tr -d '\r' > partitions.txt
host$ for p in $(grep -vE '^(userdata|super)$' partitions.txt); do
        adb exec-out su -c "dd if=/dev/block/by-name/$p bs=4M 2>/dev/null" > "$p.img"
      done
host$ sha256sum *.img > SHA256SUMS
```

Keep at least these somewhere safe (two copies): `boot_a`, `init_boot_a`, `vendor_boot_a`, `dtbo_a`, `vbmeta_a`, `nvdata`, `nvcfg`, `protect1`, `protect2`, `proinfo`, `persist`, `md_udc`, `seccfg`, `lk_a`, `preloader` (if present), and `modem_a`.

**Your stock `boot_a.img` is the "go back to Android" image.** The project's restore procedure is simply `fastboot flash boot_a boot_a.img`.

## 1.4 Vendor files you will need later

Copy these from your own phone now (they are proprietary and not in this repository):

| File | From | Used by |
|---|---|---|
| `aw882xx_acf.bin` | `/vendor/firmware/` | speaker amplifier tuning, built into the kernel (step 2) |
| Wi-Fi/BT firmware: `soc7_0_ram_mcu_1_1_hdr.bin`, `soc7_0_ram_mcu_1a_1_hdr.bin`, `soc7_0_ram_mcu_1b_1_hdr.bin`, `soc7_0_ram_bt_1_1_hdr.bin`, `soc7_0_ram_bt_1a_1_hdr.bin`, `soc7_0_ram_bt_1b_1_hdr.bin`, `BT_FW.cfg`, `wifi.cfg`, `conninfra.cfg`, `connfem.cfg` | `/vendor/firmware/` | `cmf-wifi-calibrate` (step 4) |
| Wi-Fi NVRAM (6146 bytes), installed as `cmf-wifi-nvram.bin` | `/mnt/vendor/nvdata/APCFG/APRDEB/WIFI` | Wi-Fi calibration (step 4) |
| Kinibi TEE binaries (optional) | `/vendor/bin/mcDriverDaemon`, `/vendor/app/mcRegistry/` | `cmf-kinibi` (optional) |

```bash
host$ mkdir -p vendor-files
host$ adb exec-out su -c 'tar -C /vendor -cf - firmware' > vendor-files/vendor-firmware.tar
host$ adb exec-out su -c 'cat /mnt/vendor/nvdata/APCFG/APRDEB/WIFI' > vendor-files/WIFI.nvram
```

`regulatory.db` and `regulatory.db.p7s` come from the distribution's `wireless-regdb` package.

## 1.5 Build host setup

```bash
host$ sudo apt install git build-essential bc bison flex libssl-dev libelf-dev \
        clang lld llvm device-tree-compiler gcc-aarch64-linux-gnu \
        python3 python3-cryptography zstd file mmdebstrap qemu-user-static \
        binfmt-support
```

The initramfs needs a **static arm64 busybox**. Extract Ubuntu's without installing it:
```bash
host$ sudo dpkg --add-architecture arm64 && sudo apt update
host$ apt download busybox-static:arm64 && dpkg-deb -x busybox-static_*_arm64.deb bb
host$ export BUSYBOX=$PWD/bb/bin/busybox
```
(Any static arm64 busybox with `switch_root`, `losetup`, `blockdev` and `mount` works, for example Magisk's.)

AOSP boot tools (for step 3):
```bash
host$ git clone --depth 1 https://android.googlesource.com/platform/system/tools/mkbootimg aosp/mkbootimg
host$ git clone --depth 1 https://android.googlesource.com/platform/external/avb aosp/avb
```

An AVB key of your own (any RSA-2048 key works with an unlocked bootloader):
```bash
host$ openssl genrsa -out ~/cmf-avb.pem 2048
```

Clone this repository:
```bash
host$ git clone https://github.com/rajmahavir/cmf-mainline.git
```

Next: [2. Kernel](02-kernel.md).
