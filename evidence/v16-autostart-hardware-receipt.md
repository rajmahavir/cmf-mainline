# Milestone 1: Autonomous Boot Hardware Test Receipt

**Date:** 2026-11-17  
**Target:** Nothing CMF Phone 1 (`Tetris` / `MT6878` / `A015`)  
**Objective:** Verify autonomous bootloader execution, AMOLED console framebuffer with stride 4352, and automatic Wi-Fi + SSH bringup without requiring host USB staging commands.

---

## 1. Component Artifacts & Checksums

| Component | Path | Size (Bytes) | SHA-256 Checksum |
| :--- | :--- | :--- | :--- |
| **Kernel Image** | `v16-profile/template/Image` | 37,624,320 | `482235b736fadde671214b4c6adee3b2f00471ef88fa45d39fcb1f2e3230e94c` |
| **Device Tree (DTB)** | `candidate-stride4352.dtb` | 25,969 | `20411eb84694d421a80f5405bdef7655931a7fafe1cd74495cba3e2bc5ab7254` |
| **Profile Archive** | `private-bundle.tar.gz` | 9,582,249 | `53431eadedd53bd3dfced14fb4c65bc4d3e54b5565ca0f44a53b35030035647c` |
| **Assembled Boot Image** | `out/boot.img` | 67,108,864 | `afd5463e4165d8c1a0fb0ab5327d679e41ec1bd5bd29cbc5e18c619a0e8fec1d` |

### Profile Region Layout
- Partition Offset: `32 MiB` (`0x02000000` / sector `65536`)
- Header Marker: `CMF_BOOT_PROFILE_V1`
- Declared Length: `9582249` bytes
- Payload SHA-256: `53431eadedd53bd3dfced14fb4c65bc4d3e54b5565ca0f44a53b35030035647c`

---

## 2. AVB Cryptographic Signing

The Android Verified Boot (AVB) v4 hash footer was attached and cryptographically signed via delegated OpenSSL RSA-2048 helper using `candidate-avb.pem`:
```text
Verifying image /private/tmp/cmf-v16-autostart/out/boot.img using key at candidate-avb-public.pem
vbmeta: Successfully verified footer and SHA256_RSA2048 vbmeta struct in /private/tmp/cmf-v16-autostart/out/boot.img
boot: Successfully verified sha256 hash of /private/tmp/cmf-v16-autostart/out/boot.img for image of 15458304 bytes
```

---

## 3. Fastboot Flash Execution

- Device serial: `001206477020340`
- Target slot: `boot_a`
- Command log:
  ```text
  fastboot flash boot_a /private/tmp/cmf-v16-autostart/out/boot.img && fastboot reboot
  Sending 'boot_a' (65536 KB)                        OKAY [  1.516s]
  Writing 'boot_a'                                   OKAY [  0.212s]
  Finished. Total time: 1.730s
  Rebooting                                          OKAY [  0.000s]
  ```

---

## 4. Hardware Boot Observation & Analysis

1. Device rebooted and entered MediaTek Preloader at `14:21:51` (`0x0e8d/2000`).
2. Preloader terminated after 3 seconds as expected (`14:21:54`).
3. USB ACM console (`/dev/cu.usbmodem*`) and Wi-Fi interface did not enumerate automatically.
4. **Root Cause Analysis:**
   - The compiled `v16-profile.Eab2M6/template/Image` (37.6 MB) lacks runtime validation on hardware compared to the known-good `Image-v15` (37.7 MB, sha256: `255e2ad76ce8ed54b52b66a65e3cf2870d13d4852035d36672377c024388b19d`).
   - The embedded initramfs in the template kernel stripped essential `/dev/random` and `/dev/urandom` character nodes and may have encountered an early panic before UDC binding.
5. **Recovery Target:**
   - Boot device to Fastboot via hardware keys (Power + Volume Up).
   - Restore known-good `v15` kernel with verified stride 4352 DTB (`/private/tmp/cmf-simplefb-stride4352-boot/boot.img`).
