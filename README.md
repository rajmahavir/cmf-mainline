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
* **Bootchain:** Verified on physical hardware after flashing the diagnostic image to `boot_a`. The bootloader rejects `fastboot boot`.
* **Serial Debug Console:** Interactive shell operational over USB CDC-ACM gadget (`/dev/ttyGS0`).
* **Wi-Fi & Bluetooth Stack (v13):** MediaTek out-of-tree connectivity stack (`conninfra`, `connfem`, `connadp`, `wmt_chrdev`, `wlan_drv_gen4m_6878`, `bt_drv_6878`) backported to Linux 7.2, compiled against `7.2.0-cmf-wifi-pmic+` symbols, cryptographically signed, and verified with `depmod`.
* **Device Tree:** Device tree sources include MT6878 PMIC regulator trees, fixed EINT interrupt indexing, reboot modes, and Bluetooth/Wi-Fi calibration nodes (`/bt@18800000`).
* **Radio hardware (v15):** Bluetooth and Wi-Fi pre-calibration pass. The WLAN driver creates `phy0` and `wlan0`. Passive scanning, WPA association, DHCP, gateway reachability, and SSH over Wi-Fi are verified. See the [hardware result](./evidence/v15-wlan-link.md) and [handoff](./docs/HANDOFF.md).

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
├── evidence/         # Verifiable build logs, console transcripts & test receipts
│   ├── README.md
│   ├── hardware-boot-receipt.md
│   ├── kernel-boot-build-receipt.txt
│   ├── independent-audit-receipt.json
│   ├── wlan-build-step20-receipt.txt
│   ├── bt-build-step12-receipt.txt
│   ├── v13-module-hashes-receipt.txt
│   ├── eint-verified-receipt.txt
│   ├── btf-verified-receipt.txt
│   └── qemu-console-receipt.txt
├── tools/            # Verification tooling and release gates
│   └── milestone-gate.sh
├── docs/             # Source provenance, audit logs, and QEMU console verification
│   ├── PROVENANCE.md
│   ├── qemu-console.log
│   └── v13-audit-summary.json
└── LICENSE           # GPL-2.0-only
```

---

## Boot Architecture & fdt_shim Wrapper

MediaTek Little Kernel (LK) enforces Android Verified Boot (AVB) header v4 specifications. To ensure seamless handover:
1. `fdt_shim.S` prepends the compiled Device Tree (`candidate.dtb`) to the raw AArch64 kernel Image.
2. The wrapped payload is gzip-compressed into `payload.bin.gz`.
3. The image is packaged with an AVB header matching the stock partition hash descriptor (SHA-256) so the bootloader does not reject the image.
4. During boot, the initramfs configures a single-function USB CDC-ACM device, creating an instant root shell on `/dev/ttyGS0` (appears as `/dev/ttyACM0` on the host PC).

---

## Wi-Fi and Bluetooth hardware test

Run [`tools/run-radio-test.sh`](./tools/run-radio-test.sh) from the extracted
module bundle in the diagnostic initramfs. Supply the firmware and factory
Wi-Fi NVRAM from the same handset in the bundle's `firmware/` directory.

```sh
sh run-radio-test.sh prepare
sh run-radio-test.sh calibrate
sh run-radio-test.sh power
```

The script verifies module hashes, waits for the RF PMICs, checks driver
binding, and tests calibration and interface creation in separate steps.
See the [v13 and v14 hardware results](./evidence/v13-v14-radio-bringup.md).

---

## Verification Evidence & Receipts

All claims in this repository are backed by concrete text receipts located in the [evidence/](./evidence/) directory:
* **Physical Hardware Execution:** Verified after flashing `boot_a`, with an interactive `/dev/ttyGS0` console session ([hardware-boot-receipt.md](./evidence/hardware-boot-receipt.md)).
* **Kernel & AVB Packaging:** Full build transcript with BTF and AVB header packaging ([kernel-boot-build-receipt.txt](./evidence/kernel-boot-build-receipt.txt)).
* **Independent Audit:** Structural breakdown of the boot image and wrapper ([independent-audit-receipt.json](./evidence/independent-audit-receipt.json)).
* **Connectivity Module Builds:** Clang/LLVM build logs for Wi-Fi ([wlan-build-step20-receipt.txt](./evidence/wlan-build-step20-receipt.txt)) and Bluetooth ([bt-build-step12-receipt.txt](./evidence/bt-build-step12-receipt.txt)).
* **Cryptographic Signatures & Depmod:** SHA-256 digests of all 10 compiled modules ([v13-module-hashes-receipt.txt](./evidence/v13-module-hashes-receipt.txt)).
* **Interrupt & BTF Verification:** EINT controller validation ([eint-verified-receipt.txt](./evidence/eint-verified-receipt.txt)) and BTF types ([btf-verified-receipt.txt](./evidence/btf-verified-receipt.txt)).

---

## Milestone Quality & Validation Gate

To ensure that no broken patches, undocumented binaries, or gitignored audit trails slip into public releases, run the automated Milestone Closing Gate as the final step of each milestone:

```sh
./tools/milestone-gate.sh
```

The gate automatically enforces:
1. Presence of `LICENSE` and `docs/PROVENANCE.md`.
2. Strict `.gitignore` hygiene (no gitignored evidence, binaries ignored).
3. Availability and integrity of all mandatory verification receipts.
4. Absence of accidental large binary blobs (>20MB) in git index.
5. Clean working tree and upstream branch tracking status.

---

## Provenance & License

* Kernel diffs, device trees, and build tools are released under the [GNU General Public License v2.0](./LICENSE).
* Upstream base commits, MediaTek BSP origins, and build reproducibility guidelines are detailed in [docs/PROVENANCE.md](./docs/PROVENANCE.md).
* Developed by **Raj Mahavir** and the mainline community.
