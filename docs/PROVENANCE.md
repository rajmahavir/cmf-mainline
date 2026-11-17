# Source Provenance & Build Reproducibility

This document records the exact upstream base commits, vendor BSP sources, and toolchains used to build and verify the CMF Phone 1 (`MT6878` / `Tetris`) mainline Linux 7.2 port.

---

## 1. Upstream Linux Kernel Base

* **Repository:** `https://github.com/MT6878-Mainline/linux.git`
* **Branch:** `postmarketOS/v7.2`
* **Base Commit:** `12f2e9f040d0049f3f5ce5a7e53bf5ef27b46422`
* **Commit Message:** `arm64: dts: mediatek: mt6893-pinfunc: Fix pinctrl header path`
* **Upstream Tree:** Linux 7.2.0 (based on kernel.org upstream + MediaTek arm64 community patches)
* **Configuration:** `arch/arm64/configs/defconfig` + Android/postmarketOS base config

---

## 2. Out-of-Tree MediaTek Wireless & Connectivity Stack

The connectivity drivers (`conninfra`, `connadp`, `connfem`, `wmt_chrdev_wifi_connac2`, `wlan_drv_gen4m_6878`, `bt_drv_6878`) originate from the official GPL kernel source releases for the Nothing CMF Phone 1 (MediaTek Dimensity 7300 platform):

* **Source Release:** Nothing CMF Phone 1 Open Source Kernel Modules (`kernel_device_modules-6.1`)
* **Hardware Subsystem:** MediaTek CONNAC 2.0 (`soc2_2x2` / Wi-Fi 6 + Bluetooth 5.3)
* **Port Target:** Linux 7.2.0 (`7.2.0-cmf-wifi-pmic+`)
* **Patches Applied in this Repository:**
  1. `mt6878-wifi-linux-7.2.patch`: Mainline 7.2 API adaptations for `cfg80211`, `net_device`, timer APIs, and tasklet abstractions.
  2. `mt6878-wifi-pmic.patch`: PMIC regulator supply binding and regulator framework updates for MT6368.
  3. `mt6878-eint-index.patch`: MediaTek External Interrupt (EINT) pin index mapping table fix (216 indexed slots, 137 validated entries).
  4. `mt6878-bt-calibration-mainline.patch`: Bluetooth calibration device tree node (`/bt@18800000`) and HCI interface driver.
  5. `mt6878-wifi-module-probe.patch`: Hardware probe and module auto-loading hooks.

> [!NOTE]
> This repository redistributes **only unified diffs (patches), build scripts, and device tree sources** under the GNU General Public License v2.0. No closed-source firmware or proprietary binaries are included. Firmware blobs must be supplied by the user/device into `/lib/firmware/`.

---

## 3. Toolchain & Build Environment

* **Target Architecture:** `aarch64-linux-gnu`
* **Compiler:** Clang / LLVM 18+ (`clang`, `llvm-ar`, `llvm-objcopy`, `llvm-strip`)
* **Cross-Compiler:** GCC 13+ AArch64 toolchain (`aarch64-linux-gnu-gcc`)
* **Device Tree Compiler:** `dtc` 1.7.0+
* **AVB Signing Tool:** Android Verified Boot `avbtool` (Python implementation, SHA-256 algorithm type 1)
