# Hardware Verification Receipt: SPMI RF PMIC Driver & Kernel Build #7

**Date:** 2026-11-17 19:15 IST  
**Target:** Nothing CMF Phone 1 (`Tetris` / `MT6878` / `A015`, Serial: `001206477020340`)  
**Objective:** Verify SPMI RF PMIC regulator binding (`mt636x-rf-regulator`) for MT6363 (`0-04`) and MT6369 (`0-05`), resolve ThinLTO archiving issues via GNU `ar`, boot Kernel Build #7 on slot A physical hardware, evaluate Wi-Fi pre-calibration flow, and document Method A Kali NetHunter container export.

---

## 1. Component Artifacts & Checksums

| Component | Path / Origin | Size (Bytes) | SHA-256 Checksum |
| :--- | :--- | :--- | :--- |
| **Kernel Image (Build #7)** | `192.168.3.30:/home/raj/cmf-template-fast/kernel/Image` | 30,415,360 | `9605b2d661f20df743e9fd5909c3e968f98b1c6b704c31e7571256076de72554` |
| **Device Tree (DTB)** | `candidate-stride4352.dtb` | 25,969 | `20411eb84694d421a80f5405bdef7655931a7fafe1cd74495cba3e2bc5ab7254` |
| **Compressed Payload** | `out/payload.bin.gz` | 13,220,966 | `6c47fc72cb25828450165db56fbfd64807f7cba766c0ce8ba77a166ff56f345c` |
| **Mainline Boot Image** | `out/boot.img` | 67,108,864 | `8d0087d6109f2c7064b0ff2771959d0625b964e03ae5b5de0bfb47fca74a2d93` |
| **Verified Android Rollback** | `rollback/slot-a-current/boot_a.img` | 67,108,864 | `eda9f9a5e32dcaf478e8464a473cfb0d1e447c52d97b2e76d1036ff4d7b370fb` |
| **Kali NetHunter Container** | Handset `/data/local/kali-rootfs.img` | 17,179,869,184 | 16 GiB sparse ext4 image containing the Android-side Kali copy |

---

## 2. AVB Signing & Boot Image Validation

The assembled image was verified using `avbtool` and `candidate-avb-public.pem`:
```text
Verifying image /private/tmp/cmf-build-fast/out/boot.img using key at candidate-avb-public.pem
vbmeta: Successfully verified footer and SHA256_RSA2048 vbmeta struct in /private/tmp/cmf-build-fast/out/boot.img
boot: Successfully verified sha256 hash of /private/tmp/cmf-build-fast/out/boot.img for image of 13225984 bytes
```

---

## 3. Fastboot Flash Execution (Slot A)

Pre-flight checks verified `current-slot: a` and rollback image integrity before flashing:
```text
fastboot getvar current-slot
current-slot: a
Finished. Total time: 0.003s

fastboot flash boot_a /private/tmp/cmf-build-fast/out/boot.img && fastboot reboot
Sending 'boot_a' (65536 KB)                        OKAY [  1.428s]
Writing 'boot_a'                                   OKAY [  0.198s]
Finished. Total time: 1.626s
Rebooting                                          OKAY [  0.000s]
```

---

## 4. Live Hardware Console Verification

Upon reboot, physical handset booted cleanly into Linux 7.2 userspace. Verified via interactive USB CDC-ACM serial console on `/dev/cu.usbmodem01234567891`:

### 4.1. Kernel Identification
```text
/sys/kernel/config/usb_gadget/g1 # uname -a
Linux (none) 7.2.0-cmf-wifi-pmic+ #7 SMP PREEMPT Tue Nov 17 19:04:37 IST 2026 aarch64 Android
```

### 4.2. SPMI RF PMIC Driver Binding
Verification of SPMI companion regulator slaves `0-04` (MT6363) and `0-05` (MT6369):
```text
/sys/kernel/config/usb_gadget/g1 # ls -la /sys/bus/spmi/devices/0-04/driver /sys/bus/spmi/devices/0-05/driver
lrwxrwxrwx    1 root     root             0 Jan  1 00:00 /sys/bus/spmi/devices/0-04/driver -> ../../../../../../bus/spmi/drivers/mt636x-rf-regulator
lrwxrwxrwx    1 root     root             0 Jan  1 00:00 /sys/bus/spmi/devices/0-05/driver -> ../../../../../../bus/spmi/drivers/mt636x-rf-regulator
```
Both devices are bound to `mt636x-rf-regulator`, resolving the driver dependency that stalled previous RF bring-up.

### 4.3. Storage Subsystem
```text
/sys/kernel/config/usb_gadget/g1 # ls /dev/sd*
/dev/sda    /dev/sdb    /dev/sdc
/dev/sdc1   ...         /dev/sdc83
```
All 83 partitions on UFS LUN 2 (`/dev/sdc`) enumerated successfully.

### 4.4. Loaded Connectivity Stack
```text
/sys/kernel/config/usb_gadget/g1 # lsmod
Module                  Size  Used by
conninfra             385024  4 connfem,connadp,mt6685_consys_regmap,wmt_chrdev_wifi_connac2
connfem                24576  0
connadp                16384  0
mt6685_consys_regmap   16384  0
wmt_chrdev_wifi_connac2 24576 0
cfg80211              765952  0
rfkill                 32768  1 cfg80211
```
Device node `/dev/wmtWifi` created with major/minor `10:55`.

---

## 5. Wi-Fi Pre-Calibration Analysis

Initiating Wi-Fi power-on (`printf '1' > /dev/wmtWifi`) resulted in:
```text
[MTK-WIFI] WIFI_write[E]: WMT turn on WIFI fail!
Timeout, pre_cal_flow: 0
```
**Kernel log inspection revealed the root cause:**
```text
btmtk_wcn_beif_init emi_ap_phy_base is NULL
[connsys_log_init][bt_fw] get emi config fail.
```
- MediaTek's CONNAC 2.0 architecture coordinates joint pre-calibration between Bluetooth and Wi-Fi co-processors.
- The Bluetooth driver expects the shared connsys EMI physical memory base (`emi_ap_phy_base`).
- **Remediation:** Restore the connsys EMI reserved memory node in `mt6878-tetris-wifi-radio.dts` / `mt6878-tetris-mt6685-consys.dtsi`.
- **Immediate Alternative:** ConfigFS gadget provides native USB CDC-NCM Ethernet (`ncm.usb0`) for 480 Mbps IP networking over the existing USB cable.

---

## 6. Method A: Kali NetHunter Rootfs Container Export

- **Android FBE Invariant:** Android 16 metadata encryption (`dm-default-key`, `aes-xts-plain64`) renders `/dev/sdc82` raw blocks unreadable by mainline Linux without hardware Keymaster key derivation.
- **Implementation:**
  1. Booted Android 16 with Magisk root.
  2. Created 16 GB sparse ext4 container: `truncate -s 16G /data/local/kali-rootfs.img` and `mke2fs -t ext4 -F -O ^has_journal /data/local/kali-rootfs.img`.
  3. Bypassed SEAndroid loop device I/O restrictions via temporary `setenforce 0`.
  4. Mounted on `/mnt/kali_target` and copied 13 GB chroot: `cp -a /data/local/nhsystem/kali-arm64/* /mnt/kali_target/`.
  5. Unmounted cleanly, detached loop device, and restored SELinux to `Enforcing` (`setenforce 1`).
  6. Verified Android 16 stability; original chroot remains completely functional in Android.

The ext4 image file remains in encrypted Android userdata. The
documented source glob normally skips top-level hidden entries.
The receipt records the Android copy and cleanup. Off-device export
and source-to-image validation remain separate milestones.
