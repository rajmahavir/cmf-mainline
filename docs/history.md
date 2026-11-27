# Project history

The port went from a black screen to a daily-usable phone in about two weeks of November 2026, in small numbered stages. Every stage was a flash of `boot_a` with a rollback image ready. This is the condensed story, kept because the lessons are useful for other MediaTek phones.

## Early diagnostic builds (v1–v16, ~10–17 Nov)
- **Getting any output.** LK rejects `fastboot boot` and hands over its own DTB, so the first milestone was the **shim**: a tiny Image-header stub that swaps in our DTB. A **USB CDC ACM gadget with a root shell** (v7–v9) became the main debugging tool for the rest of the project. (v8 lesson: `PATH` is unset in an initramfs; v9 calls the shell by absolute path.)
- **Display** first through `simpledrm` on LK's framebuffer (pitch aligned to 4352 bytes), with a 16×32 console font.
- **UFS** fully enumerated; **SPMI RF PMIC regulators** (MT6363/MT6369) ported.
- **Wi-Fi/BT:** the MediaTek connectivity stack from the vendor GPL release was ported to Linux 7.2 as out-of-tree modules; Build #8 fixed the connectivity power domain and reached calibration, association and SSH.

## Kali era (~17–23 Nov)
- A Kali NetHunter root on the microSD, entered by chroot from the initramfs, with a private "boot profile" in the boot image carrying modules and credentials.
- **Native display** (`mediatek-drm` DSI) replaced simpledrm, with display power-domain save/restore (Stages 82–86), then **GPU (Panthor), keys and battery built in** (Stage 87), Xorg → XFCE → Phosh.
- **Cellular:** CCIF clocks, modem resources and the CCCI driver (from Serlk's Titan 2 work), the LK modem-record handoff, DPMAIF data and IMS: 5G data and VoLTE calls with audio.

## Ubuntu and native boot (23–24 Nov)
- The SD root switched from Kali to **Ubuntu 26.04 + Phosh** built with `mmdebstrap` (Stage 88: Phosh started directly).
- Stage 89 built the modem drivers in; Stage 90 added Kinibi TEE support.
- **Stage 91: native boot.** `/init` `switch_root`s into **systemd as PID 1**; the chroot glue became systemd units (Wi-Fi calibration, modem, speech, Bluetooth, audio). Phosh runs as a normal user.
- Stage 92: FUSE as a signed module; Stage 93: suspend groundwork.

## Internal storage (25–26 Nov, Stage 94)
- Android reserves a pinned, immutable 25 GiB file in `/data`; Linux maps its extents with dm-linear. Stamps per 2 MiB segment detect any change; a crash guard falls back to the SD. Verified across an Android round trip.

## Suspend and robustness (26–27 Nov, Stage 95)
- 95a: IRQ-driven PMIC keys. Real s2idle reached for 66 s with systemd holding the watchdog, waking on an incoming call.
- Two unclean resets left the internal root in emergency mode. The cause of the damage: the crash guard's `ro,noload` probe mount **carried `noload` into the live root, so it ran without its ext4 journal**. **Stage 95c (init v12)** remounts with the journal and re-checks the crash marker on the replayed view. QEMU tests showed v11 would even miss a marker that existed only in the journal.
- Emergency mode now keeps a serial root shell; logs stream off-device because RAM pstore doesn't survive resets.

## Lessons
1. Get a **serial console** working before anything else, and keep it working in every failure mode.
2. **Flash one partition**, keep the rollback image and check hashes every time.
3. Vendor drivers with **one-shot probes** need explicit ordering; don't let udev race them.
4. **Mount options survive remounts**: `noload`/`norecovery` from a probe mount silently disabled the journal.
5. **Test failure paths** (crashes, corrupted journals) in QEMU with fixture disks before they happen on the phone.
6. Credit and reuse the work of people porting the same SoC: Serlk's Titan 2 port saved weeks on the modem and audio.
