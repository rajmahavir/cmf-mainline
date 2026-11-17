# CMF Phone 1 (MT6878 / Tetris) — Mainline Linux Port

Upstream **Linux 7.2** mainline port for the **Nothing CMF Phone 1** (MediaTek Dimensity 7300 / MT6878).

---

## Hardware Specification

| Component | Detail |
|---|---|
| **Device** | Nothing CMF Phone 1 (`Tetris` / `A015`) |
| **SoC** | MediaTek Dimensity 7300 (`MT6878`) |
| **CPU** | Octa-core: 4x Cortex-A78 @ 2.5 GHz + 4x Cortex-A55 @ 2.0 GHz |
| **RAM** | 8 GB LPDDR4X |
| **Storage** | 128 / 256 GB UFS 2.2 |
| **GPU** | ARM Mali-G615 MC2 |
| **Display** | 6.67" 1080x2400 120Hz Super AMOLED |
| **Connectivity** | MT6878 CONNAC 2.0 (`soc2_2x2` / Wi-Fi 6 + BT 5.3) |
| **Power Management** | MediaTek MT6368 PMIC |

---

## Current Status

* **Core Kernel:** Linux 7.2.0 (`postmarketOS/v7.2` branch).
* **Bootchain:** Verified on physical hardware in RAM via `fastboot boot`.
* **Serial Debug Console:** Interactive shell operational over USB CDC-ACM gadget (`/dev/ttyGS0`).
* **Wi-Fi & Bluetooth Stack (v13):** MediaTek out-of-tree connectivity stack (`conninfra`, `connfem`, `connadp`, `wmt_chrdev`, `wlan_drv_gen4m_6878`, `bt_drv_6878`) backported to Linux 7.2, compiled against `7.2.0-cmf-wifi-pmic+` symbols, cryptographically signed, and verified with `depmod`.
* **Device Tree:** Device tree sources include MT6878 PMIC regulator trees, fixed EINT interrupt indexing, reboot modes, and Bluetooth/Wi-Fi calibration nodes (`/bt@18800000`).

---

## Repository Structure

```text
cmf-mainline/
├── boot/             # AVB header generation, linker scripts, and fdt_shim entrypoint
│   ├── build_diagnostic_v9.py
│   ├── fdt_shim.S
│   ├── wrapper.ld
│   ├── avb-verification.txt
│   ├── independent-audit.json
│   └── manifest.json
├── dts/              # Device Tree sources and includes
│   ├── v9-device-tree.dts
│   ├── mt6878-tetris-wifi.dtsi
│   ├── mt6878-tetris-wifi-calibration.dts
│   ├── mt6878-tetris-bt-calibration.dtsi
│   ├── mt6878-tetris-reboot-mode.dtsi
│   └── mt6878-tetris-wifi-radio-test.dtsi
├── patches/          # Linux 7.2 kernel patches
│   ├── mt6878-wifi-linux-7.2.patch
│   ├── mt6878-wifi-pmic.patch
│   ├── mt6878-eint-index.patch
│   ├── mt6878-bt-calibration-mainline.patch
│   └── mt6878-wifi-module-probe.patch
├── initramfs/        # Minimal initramfs and device node configurations
│   ├── init          # Busybox init with USB gadget CDC-ACM setup
│   └── dev_nodes.txt
├── wifi-bt/          # Module export scripts and dependency definitions
│   ├── BUILD-INFO.txt
│   ├── wlan-dependency-order.txt
│   ├── export-wifi-v13-modules.sh
│   ├── package-wifi-pmic.py
│   ├── rebuild-wifi-for-v11.sh
│   ├── rebuild-wifi-for-pmic.sh
│   └── verify-v11-eint.py
└── docs/             # Audit logs and QEMU console verification
    ├── qemu-console.log
    └── v13-audit-summary.json
```

---

## Boot Architecture & fdt_shim Wrapper

MediaTek Little Kernel (LK) enforces Android Verified Boot (AVB) header v4 specifications. To ensure seamless handover:
1. `fdt_shim.S` prepends the compiled Device Tree (`candidate.dtb`) to the raw AArch64 kernel Image.
2. The wrapped payload is gzip-compressed into `payload.bin.gz`.
3. The image is packaged with an AVB header matching the stock partition hash descriptor (SHA-256) so the bootloader does not reject the image.
4. During boot, the initramfs configures a single-function USB CDC-ACM device, creating an instant root shell on `/dev/ttyGS0` (appears as `/dev/ttyACM0` on the host PC).

---

## Wi-Fi & Bluetooth Module Load Sequence

To initialize the CONNAC 2.0 wireless subsystem, the signed modules must be loaded in exact dependency order:

```sh
# 1. Connectivity bus adapter & infrastructure
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/connadp.ko
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/conninfra.ko

# 2. RF Kill & Front-End Module
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/rfkill.ko
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/connfem.ko

# 3. Wireless character device & 802.11 core
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/wmt_chrdev_wifi_connac2.ko
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/cfg80211.ko

# 4. Primary Wi-Fi driver
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/wlan_drv_gen4m_6878.ko

# 5. Bluetooth subsystem & MTK BT driver
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/bluetooth.ko
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/cmac.ko
insmod lib/modules/7.2.0-cmf-wifi-pmic+/extra/bt_drv_6878.ko
```

---

## License & Credits

* Linux kernel patches are licensed under **GPL-2.0-only**.
* MediaTek connectivity modules are based on MediaTek Open Source releases.
* Developed by **Raj Mahavir** and contributors.
