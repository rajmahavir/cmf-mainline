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

### 3.1 Verified toolchain on the build host (2026-11-18)

Recorded by running the tools, not from memory:

| Tool | Version |
| --- | --- |
| `clang` | Ubuntu clang 21.1.8 (`6ubuntu1`), target `x86_64-pc-linux-gnu` |
| `ld.lld` | Ubuntu LLD 21.1.8 |

ThinLTO links at this version require `DEBUG_INFO_BTF_MODULES=off` and
`DEBUG_INFO_NONE` plus `--strip-debug`; see
[`V16-AUTOSTART.md`](./V16-AUTOSTART.md) for the linker invariants.

---

## 4. Vendor BSP fingerprint

The three vendor patches apply to one specific unpacking of Nothing's published
CMF Phone 1 kernel-module sources. That tree is not vendored here, so it is
identified by content. From the BSP's connectivity directory:

```sh
cd <bsp-root>/vendor/mediatek/kernel_modules/connectivity
find . -type f \( -name '*.c' -o -name '*.h' -o -name Kbuild -o -name Makefile \) -print0 \
  | xargs -0 sha256sum | LC_ALL=C sort -k2 | sha256sum
```

| Field | Value |
| --- | --- |
| Source files hashed | 4433 (`.c`, `.h`, `Kbuild`, `Makefile`) |
| Aggregate SHA-256 | `1c5d154b9d3c29ba556f2774c016c4900db226392bb27a4062c04739e927a821` |
| Connectivity tree size | 210 MB |
| Files modified by the vendor patch set | 74 (+545 / −470) |

If the aggregate hash differs, the vendor patches are expected to fail on
context; re-derive them with `scripts/export-patches.sh` rather than fuzzing.

---

## 5. Patch application contract

Each patch has a distinct application root, and all six were re-verified against
pristine trees on 2026-11-18:

* Matrix, reproduction commands and common failure modes:
  [`../patches/APPLY-STATUS.md`](../patches/APPLY-STATUS.md)
* Integrity of the files themselves: [`../patches/MANIFEST.sha256`](../patches/MANIFEST.sha256)
* Automated application: [`../scripts/apply-patches.sh`](../scripts/apply-patches.sh)

Kernel patches target `12f2e9f040d0049f3f5ce5a7e53bf5ef27b46422` and are expected
to apply cleanly in this order: `mt6878-eint-index`, `mt6878-wifi-pmic`,
`mt6878-connectivity-power-domain`.

---

## 6. Build identity

A flashable state is identified by all three of these, never by a build number
alone:

| Build | Kernel `vermagic` | Image SHA-256 | Verified on disk 2026-11-18 |
| --- | --- | --- | --- |
| #7 | `7.2.0-cmf-wifi-pmic+` | `9605b2d661f20df743e9fd5909c3e968f98b1c6b704c31e7571256076de72554` | `build-box:~/cmf-template-fast/Image` — 30,415,360 B, hash matches |
| #8 | `7.2.0-cmf-wifi-pmic+` | `3c95c484f1237748b4b2dc03357690c619c4b9e7d3c9431d281e43338a83cce0` | `build-box:~/cmf-template-fast/conn-domain-fix.tTXm2T/Image` — 30,415,360 B, hash matches |

Both kernel images behind the Wi-Fi milestone are present on the build host and
match their recorded SHA-256. One documentation defect was found while checking:
`evidence/v16-hardware-rf-pmic-receipt.md` (and the Build #7 appendix in
`docs/HANDOFF.md`) record it as `~/cmf-template-fast/kernel/Image`, which does
not exist — the file sits one level up at `~/cmf-template-fast/Image`. The
receipt is left unedited (it is a
raw record of what was written at the time); the correct location is this table,
and `scripts/verify-evidence.sh` reports such drift as `MOVED` instead of
skipping it.

Both images are the same size, which is worth remembering when comparing them:
only the hash distinguishes Build #7 from Build #8.

Build #8 reuses the Build #7 device tree
(`20411eb84694d421a80f5405bdef7655931a7fafe1cd74495cba3e2bc5ab7254`); the
module set must match the kernel build that produced `kernel.release`, which is
why the export scripts pin `7.2.0-cmf-wifi-pmic+` paths.

