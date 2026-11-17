# Verification Evidence & Receipts

This directory contains verifiable text receipts, build logs, and console transcripts proving hardware execution and driver validation on the Nothing CMF Phone 1 (`MT6878` / `Tetris`).

---

## Index of Receipts

| Receipt File | Verification Scope | Status |
|---|---|:---:|
| **[hardware-boot-receipt.md](./hardware-boot-receipt.md)** | Physical CMF Phone 1 RAM boot via fastboot & interactive `/dev/ttyGS0` serial session | **VERIFIED ON HARDWARE** |
| **[kernel-boot-build-receipt.txt](./kernel-boot-build-receipt.txt)** | Linux 7.2 kernel compilation, BTF ID generation, fdt_shim wrapper assembly, and AVB header packaging | **BUILD VERIFIED** |
| **[independent-audit-receipt.json](./independent-audit-receipt.json)** | Independent structural audit of boot.img, AVB v4 headers, wrapper headers, and decompressed payloads | **AUDITED** |
| **[wlan-build-step20-receipt.txt](./wlan-build-step20-receipt.txt)** | LLVM Clang compilation & linking of `wlan_drv_gen4m_6878.ko` against Linux 7.2 | **BUILD VERIFIED** |
| **[bt-build-step12-receipt.txt](./bt-build-step12-receipt.txt)** | Compilation & linking of MediaTek `bt_drv_6878.ko` against Linux 7.2 | **BUILD VERIFIED** |
| **[v13-module-hashes-receipt.txt](./v13-module-hashes-receipt.txt)** | Cryptographic SHA-256 digests and depmod resolution for all 10 connectivity modules | **AUDITED & SIGNED** |
| **[eint-verified-receipt.txt](./eint-verified-receipt.txt)** | MediaTek MT6878 EINT external interrupt controller pin slot validation (216 slots) | **VERIFIED** |
| **[btf-verified-receipt.txt](./btf-verified-receipt.txt)** | Kernel BTF symbol verification for Linux 7.2 | **VERIFIED** |
| **[qemu-console-receipt.txt](./qemu-console-receipt.txt)** | QEMU AArch64 simulation reaching userspace initramfs | **SIMULATION VERIFIED** |
| **[v13-v14-radio-bringup.md](./v13-v14-radio-bringup.md)** | Hardware bringup of MT6685 SPMI clock strap provider and radio test suite | **VERIFIED ON HARDWARE** |
| **[v14-phy-startup-comparison.md](./v14-phy-startup-comparison.md)** | Curated PHY comparison between Android and mainline firmware exchange | **VERIFIED** |
| **[v15-display-framebuffer.md](./v15-display-framebuffer.md)** | SimpleFB 1080x2400 @ 32bpp (stride 4352) and 16x32 console font verification | **VERIFIED ON HARDWARE** |
| **[v15-wlan-link.md](./v15-wlan-link.md)** | Wi-Fi passive scan, WPA association, DHCP, and SSH server execution | **VERIFIED ON HARDWARE** |
| **[v16-autostart-hardware-receipt.md](./v16-autostart-hardware-receipt.md)** | Milestone 1 autonomous bootloader execution and profile partition test | **VERIFIED ON HARDWARE** |
| **[v16-hardware-rf-pmic-receipt.md](./v16-hardware-rf-pmic-receipt.md)** | SPMI MT6363/MT6369 RF regulator binding, Kernel Build #7 hardware boot, and Kali rootfs container | **VERIFIED ON HARDWARE** |
