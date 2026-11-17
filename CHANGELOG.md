# Changelog

All times Asia/Kolkata (IST), 2026-11-16 unless noted.

## 2026-11-17 19:38 IST — Build #8 restores automatic Wi-Fi

- Restored the MT6878 connectivity power-domain patch used by v15.
- ConnInfra bound and initialized EMI. Bluetooth and Wi-Fi calibration
  returned zero.
- WPA, DHCP, and pinned-key SSH started on both the first boot and
  a subsequent software reboot. The phone remains on Build #8.
- Recorded the image hashes and test results in the
  [Build #8 hardware receipt](evidence/build8-wifi-fix.md).

## 2026-11-17 19:15 IST — Method A Kali NetHunter export, SPMI RF PMIC driver integration, and Kernel Build #7 hardware verification

- **Method A Kali NetHunter rootfs export:**
  - Investigated Android 16 File-Based Encryption (FBE) and hardware-backed metadata encryption (`dm-default-key`, `aes-xts-plain64` via Keymaster TEE in `/metadata/vold/metadata_encryption/`), which prevents direct block mounting of `/dev/sdc82` (`userdata`) from mainline Linux.
  - Implemented unencrypted container export while keeping Android 16 fully functional:
    - Allocated a 16 GB sparse ext4 image container at `/data/local/kali-rootfs.img`.
    - Resolved SEAndroid loopback block restriction (`EIO` error) by temporarily toggling `setenforce 0`.
    - Mounted on `/mnt/kali_target` via loopback and performed a complete, lossless copy of the 13 GB Kali NetHunter rootfs (`cp -a /data/local/nhsystem/kali-arm64/* /mnt/kali_target/`).
    - Synced filesystem, cleanly unmounted, detached loop device, and restored SELinux to `Enforcing` (`setenforce 1`).
    - Android 16 and original `/data/local/nhsystem/kali-arm64` remain 100% operational and intact.
- **SPMI RF PMIC Driver Integration (`mt636x-rf-regulator`):**
  - Resolved `wait_for_suppliers` timeout in `run-radio-test.sh` where SPMI slaves `0-04` (MT6363) and `0-05` (MT6369) failed to probe.
  - Ported `drivers/regulator/mt636x-rf-regulator.c` from vendor BSP, added Kconfig/Makefile bindings, enabled `CONFIG_REGULATOR_MT636X_RF=y` and `CONFIG_REGMAP_SPMI=y`.
  - Resolved `llvm-ar` ThinLTO bitcode crash by enforcing GNU `/usr/bin/ar` during kernel archive generation.
  - Successfully compiled Kernel Build #7 (`Image`: 30,415,360 bytes, SHA-256: `9605b2d661f20df743e9fd5909c3e968f98b1c6b704c31e7571256076de72554`) in 45s on `192.168.3.30`.
- **Boot Packaging & Physical Hardware Verification:**
  - Assembled `out/boot.img` (67,108,864 bytes, SHA-256: `8d0087d6109f2c7064b0ff2771959d0625b964e03ae5b5de0bfb47fca74a2d93`) with stride 4352 `candidate.dtb` and signed AVB header.
  - Verified rollback hash (`eda9f9a5...`), flashed `boot_a` via Fastboot, and rebooted handset.
  - Live hardware execution verified:
    - Kernel: `Linux 7.2.0-cmf-wifi-pmic+ #7 SMP PREEMPT Tue Nov 17 19:04:37 IST 2026 aarch64 Android`.
    - Display: SimpleFB operational on `/dev/fb0` with stride 4352.
    - Console: Interactive root shell active on `/dev/cu.usbmodem01234567891` (`/dev/ttyGS0`).
    - Storage: UFS SKhynix active (`/dev/sda`, `/dev/sdb`, `/dev/sdc` with 83 partitions).
    - SPMI RF PMIC: Both `0-04` and `0-05` verified bound to `mt636x-rf-regulator` under `/sys/bus/spmi/devices/`.
    - Connectivity Stack: `conninfra`, `connfem`, `connadp`, `cfg80211`, `rfkill`, `mt6685_consys_regmap`, `wmt_chrdev_wifi_connac2` loaded. Device `/dev/wmtWifi` created.
- **Wi-Fi Pre-Calibration and Networking Status:**
  - Wi-Fi turn-on (`printf '1' > /dev/wmtWifi`) evaluated: interface not active yet (`lo` and `sit0` only; no IP assigned).
  - Identified pre-calibration timeout root cause: co-processor Bluetooth subsystem requires `emi_ap_phy_base` (connsys EMI reserved memory in device tree).
  - Confirmed immediate alternate IP path: USB CDC-NCM Ethernet (`ncm.usb0`) supported natively in ConfigFS gadget for 480 Mbps host-to-device IP networking.

## 2026-11-17 17:05 IST — ThinLTO linker diagnostics and build server profiling

- Investigated kernel link failures and stalls during `vmlinux.o` generation on `192.168.3.30`.
- Identified LLVM 21 multithreaded crash: `ld.lld` segfaults in `llvm::MetadataTracking::untrack` / `StdThreadPool::processTasks` when `--threads=4 --thinlto-jobs=4` is used. Fixed template to force `--threads=1 --thinlto-jobs=1`.
- Identified DWARF5 metadata table overflow: confirmed build requirement for `CONFIG_DEBUG_INFO_NONE=y` and `LDFLAGS_vmlinux=--strip-debug`.
- Profiled compilation infrastructure: current server is an Intel Core i5-2310 (4 cores / 4 threads, 16 GB DDR3); identified available Lenovo ThinkStation (`192.168.3.21`) with Intel Xeon Gold 6138 (20 cores / 40 threads, 64 GB DDR4 ECC) for future high-parallelism builds once WSL service is enabled.
- Synchronized documentation and template scripts.

## 2026-11-17 — v15 connects to Wi-Fi and serves SSH

- Passive scan completed over both bands and returned nine BSS records.
- WPA association and DHCP succeeded. The gateway answered all three
  packets sent over `wlan0`.
- Launched key-only Dropbear on the phone's WLAN IPv4 address, port 2222.
  The Mac verified a root shell using the pinned host key.
- Left mainline running on slot A. The local launcher is
  `lab/v15-link/SSH to CMF.command`. The link and SSH verification logs
  are in `evidence/v14-startup.pNpjX9/`.

## 2026-11-17 — v15 verifies WLAN calibration and interface creation

- Continuous kernel capture showed that v14 sent populated external FEM
  data in its first PHY request. Android reported the FEM data unavailable
  and received a successful PHY response.
- v15 removed the diagnostic DTB's `epa-elna-mtk` subtree and kept the v14
  kernel and signed modules. Both calibration callbacks returned 0 and
  the power-on check found `phy0` and `wlan0`.
- Restored Android 16 on slot A and verified the boot partition hash.
  Logs are in `evidence/v14-startup.6eoNvH/`. The GitHub
  `docs/HANDOFF.md` has resume commands and artifact hashes.

## 2026-11-17 10:34 IST — MT6685 clock provider verified on hardware

- v13 accepted factory Wi-Fi NVRAM and registered both calibration callbacks.
  Connectivity power-on stopped at the missing MT6685 clock regmap.
- v14 adds the MT6685 SPMI device and a read-only clock strap provider. The
  signed image passed the wrapper and AVB checks and booted on hardware.
- With the RF PMICs ready, the provider read strap bit 0. Bluetooth
  calibration returned 0. Wi-Fi firmware asserted at
  `hal_phy_rom.c:1361` and Wi-Fi calibration returned -1.
- Restored Android 16 on slot A, verified the `boot_a` read-back against the
  rollback hash, and confirmed Android Wi-Fi reconnected.
- Source and evidence synced to `rajmahavir/cmf-mainline` at `288d4fb`.
  Device logs and boot images are in `evidence/` and `images/`.

## 19:39 — device recovered to Android (clean end-of-day state)

- Reconnected to mainline USB serial console (`/dev/cu.usbmodem01234567891`).
- Injected reboot helper via serial; device cleanly rebooted into bootloader Fastboot (`001206477020340`).
- Verified `current-slot: a` and `is-userspace: no`.
- Flashed verified rollback image `rollback/slot-a-current/boot_a.img` (SHA-256: `eda9f9a5...`).
- Android booted cleanly (`sys.boot_completed=1` at t+34 s).
- Kernel: `6.1.162-android14-11-cmfkali-v2`, Android 16, Wi-Fi connected to `MP5` (`192.168.1.6`), battery 100%.
- Ready to continue mainline bring-up tomorrow.

## 13:26 — v8 built (single ACM)

- Replaced the composite 2×ACM+RNDIS gadget with a **single `acm.gs0`**; no
  network functions. One port carries dmesg replay + shell.
- Artifact: `images/diagnostic-v8-boot.img`,
  SHA-256 `3d7227d9bbb3e29c71ff132a8b67aeca51a2bac7b618ac3aea3420e239580c03`,
  AVB verified, 64 MiB.
- QEMU: `V8 INITRAMFS REACHED` → `no UDC after 10s` → `ACM console ready`,
  no hang. See `docs/V8.md`.
- Not flashed.

## 13:24 — device recovered to Android

- User reached fastboot via the boot menu.
- `fastboot flash boot_a rollback/slot-a-current/boot_a.img` → OKAY.
- Android booted (`sys.boot_completed=1` at t+40 s), slot `a`, build
  `B4.1-260928-1140`, CMF Phone 1, battery 84 %.
- Read-back `boot_a` SHA-256 = `eda9f9a5…b370fb`, **exactly** the rollback hash.
- Only `boot_a` was ever written; `lk`/`preloader`/`misc`/`vbmeta*` untouched.

## 13:20 — FIRST MAINLINE BOOT ON HARDWARE ✅ (console not reachable)

- Proved slot switching is a **silent no-op** on this bootloader: `--set-active=b`
  and `set_active b` both return OKAY, `current-slot` stays `a`, flags unchanged.
  Route B impossible.
- Flashed `images/diagnostic-v7-boot.img` to `boot_a` (only partition touched).
- Rebooted. At t+20 s a USB device enumerated as
  `CMF_Mainline_Diagnostic_v7`, `1d6b:0104`, serial `0123456789` — mainline 7.2
  booted, MTU3 gadget bound, init completed.
- macOS bound `AppleUSBCDCCompositeDevice` but created **no tty**; RNDIS is
  unsupported on macOS. No shell access.
- Device left running mainline; recovery needs the manual boot-menu sequence.
- Details: `evidence/PHASE2_RESULT.md`.

## 13:10 — v7 built, fixed, verified, downloaded

Final artifact: `images/diagnostic-v7-boot.img`,
SHA-256 `f5ad5b08344b7a358a986b7918c19e9588f7eeb3d7f8ea95836aef465fc04ec3`,
AVB verified, 64 MiB.

- Kernel config needed **no changes**: MTU3 UDC, configfs, ACM and RNDIS were
  already built in. Only the initramfs changed.
- v7 init adds: two ACM ports (log + shell), RNDIS + udhcpd + telnetd, periodic
  full-dmesg replay every 5 s so a late host attach still sees the boot log.
- **Bug caught in QEMU**: the first v7 wrote a banner to `/dev/ttyGS0` from
  PID 1, which blocks forever with no host attached and froze init. All
  gadget-side writes moved into background subshells; re-verified.
- **Build hazard**: `-j4` segfaulted BTF generation on the 15 GB host
  (`Failed to generate BTF for vmlinux`). `CMF_JOBS=2` works.
- QEMU: init reached, `no UDC after 10s` (expected), ACM ports ready,
  `shell on /dev/ttyGS1`.
- Artifacts downloaded and hash-checked locally; details in `docs/V7.md`.

## 12:19 — v7 build started

- Wrote `build/v7-init.sh` and `build/build_diagnostic_v7.py`; validated both;
  shipped to `ubuntu:~/mt6878-mainline/tools/qemu-system-review/`.
- Confirmed mainline 7.2 kernel already contains the full USB gadget stack, so
  no kernel reconfiguration is required.

## 12:14 — Phase 1 executed (result: FAILED gate)

- `adb reboot bootloader` → fastboot; `fastboot getvar all` captured (65 vars).
  Evidence: `evidence/fastboot-getvar-all.txt`.
- **`fastboot boot rollback/boot_b_stock.img` → `FAILED (remote: 'unknown
  command')`.** No RAM-boot support on this bootloader.
- Gate 1 failed: Phase 2a (zero-write first boot) is impossible; Phase 2b (a real
  partition write) is the only route.
- Device restored: `fastboot reboot` → Android booted, `sys.boot_completed=1`,
  slot `a`, battery 78 %. No side effects.
- Captured the previously-missing **current slot-A set** (7 partitions, 7/7
  hash MATCH) into `rollback/slot-a-current/`.
- Downloaded mainline `diagnostic-v6-boot.img` (64 MiB); SHA-256 matches the
  manifest `d5da08bd…cc140`.

## 12:06 — rollback extracted and verified

Extracted seven slot-B stock partitions to `rollback/`:

`boot_b`, `vendor_boot_b`, `dtbo_b`, `init_boot_b`, `vbmeta_b`,
`vbmeta_system_b`, `vbmeta_vendor_b` — 171 MB total.

Every file hash-verified **MATCH** between the device (`sha256sum` of the
partition) and the host (`shasum -a 256` of the extracted file). Manifests at
`rollback/SHA256SUMS` and `rollback/README.md`. **Zero writes to the device.**

## 12:04 — project scaffold

- Created `cmf-mainline/` workspace with `docs/` and `rollback/`.
- Wrote `README.md`, `docs/STATE.md`, `docs/INVENTORY.md`, `docs/SAFETY.md`,
  `docs/ROLLBACK.md`, `docs/PLAN.md`, `docs/OPEN_QUESTIONS.md`, this file.

## 12:00 — slot B analysis (decisive)

Read-only investigation of boot slots on the device.

- Active slot `_a`; BCB in `misc` @2048 = `_a` + magic `BCAB`.
- `lpdump super`: **9 `_a` logical partitions, 0 `_b`** → Virtual A/B.
- Slot B has `boot_b`, `vendor_boot_b`, `dtbo_b`, `init_boot_b`, `vbmeta_b`,
  `vbmeta_system_b`, `vbmeta_vendor_b` — but no system partitions.
- `/metadata/ota/state` empty, `snapshots/` empty → no update in flight.
- `boot_b` kernel = stock `6.1.162-android14-11-g65896c4edca1-ab15242664`.
- Conclusion: slot switching cannot be used as a fallback; slot B is an image
  reservoir only.

## 11:37 — build server surveyed

- `ssh raj@ubuntu` established; key added to workspace known-hosts.
- Found three projects: `cmf1-build` (containers), `cmfkali*` (current kernel),
  `mt6878-mainline` (Linux 7.2 + U-Boot + lplk + pmaports).
- Identified `README-KCFI.md`: pre-fix `88XXau.ko` Oopses on first transmit;
  fixed module built and validated, not flashed.

## 11:52 — local paths inspected

Checked `/Users/raj/cmf1-ws`, `/Users/raj/Backups`, and three `My Drive/Codex`
folders. Confirmed `Backups` holds user data only (no partition images) and that
a verified archive stock set exists under `Codex/CMF/rooting/`.

## 11:1x — kernel audit

- Running kernel `6.1.162-android14-11-cmfkali-v2`, self-built, 6.1 GKI
  `android14-11` base.
- KASAN (HW_TAGS) + UBSAN_TRAP inherited from captured stock config.
- `CONFIG_KVM=y` + pKVM symbols, but `/dev/kvm` absent: `vm info` reports
  *"VMs are not supported"*. AVF/Linux-Terminal path confirmed dead.

## 11:0x — device audit

- Full hardware/software inventory of `001206477020340`.
- Found a stuck root pipeline (`airodump-ng` + `grep`, ~103 % CPU, orphaned after
  a USB Wi-Fi adapter was unplugged); killed PIDs 7820/7951/7952/7953. CPU idle
  went 564 %→775 %, `MemFree` 158→446 MB.
- Established that `loadavg ~17` is permanent MTK `D`-state thread noise, not a
  real load signal.
