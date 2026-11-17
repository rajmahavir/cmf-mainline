# v16 automatic Wi-Fi and SSH

v16 uses the v15 device tree and the same signed radio modules. Its
initramfs starts the existing preparation, calibration, power-on, WPA,
DHCP, and SSH checks in that order. The USB serial shell remains
available for recovery and diagnostics. The Wi-Fi startup runs without
a USB console connection.

## Build and personalize

1. On Ubuntu, run `boot/build-autostart-template.sh` with the v15 Linux
   source, matching kernel build directory, v9 BusyBox file, v15 DTB, and
   a new output directory. It copies the baseline build and relinks a
   kernel `Image` with a small gzip initramfs. The template contains no
   per-device radio files, network profile, or keys.
2. On the Mac, unpack the verified local v15 link bundle into a private
   directory. Update `run-radio-test.sh` from the checkout and add the
   local `reboot-bootloader` helper. Include a private `wpa.conf`, the
   OpenSSH public key as `cmf_authorized_keys`, and a Dropbear Ed25519
   host key as `cmf_dropbear_host_key`. The host key stays the same across
   reboots of this image. Create a gzip tar archive of the bundle.
3. Run `wifi-bt/package-wifi-pmic.py` with the verified v9 boot image,
   template kernel, v15 DTB, packaging-tools directory, and private
   output directory. Pass the archive with `--private-profile`.
   Use `--avb-key` with the existing AVB public key and `--signing-helper`
   to delegate signing to Ubuntu. Only the AVB signing block goes to
   Ubuntu. The packager verifies the kernel payload and the profile in the
   signed image.

The profile begins at 32 MiB in the same `boot_a` partition, after the
kernel payload declared in the Android boot header and the AVB vbmeta struct.
The kernel payload is verified by LK via AVB, and the profile is verified
cryptographically via its SHA-256 digest before extraction. The initramfs
finds the `boot_a` block device by its partition name, checks the header and
profile SHA-256, and reads and extracts the archive in RAM. Startup performs
no block-device writes.

The personalized image and the flashed `boot_a` contain the Wi-Fi
credential, hardware calibration files, and private SSH host key. Keep the
image in private local storage. The GitHub repo holds the build process,
template sources, and curated verification records. The Mac's SSH
client private key stays on the Mac.

## Recovery

Verify the local Android rollback image before writing. Flash only
`boot_a` with the phone in bootloader Fastboot on slot A. From the USB
serial shell, `sync; /tmp/cmf-v13/reboot-bootloader` returns to Fastboot.
Use manual bootloader entry from the local rollback guide if the
serial shell is unavailable. Restore the verified Android `boot_a` image,
then verify Android boot and the partition hash.

## Hardware checkpoint

v15 passed a 30-packet gateway test with zero losses and one duplicate.
A 32 MiB download over SSH matched the expected SHA-256. A forced WPA
disconnect temporarily took SSH offline. Reconnection restored WPA
`COMPLETED`, gateway reachability, and SSH.

The first v16 prototype embedded a 12 MiB slot inside the kernel. It
flashed successfully but produced no observable SSH, USB serial, or
Fastboot endpoint after reboot. The failure stage is unknown. Hardware
Fastboot entry recovered the phone, and v15 Wi-Fi and SSH were restored
and verified. The revised template keeps the kernel payload near v15's
size. The running v15 kernel successfully read the proposed profile
offset, and delegated AVB signing covered a synthetic profile in
packaging tests. The revised image still needs its hardware boot test.

The local recovery receipt is under
`cmf-mainline/evidence/v16-autostart.EXE2Qs/`. The runtime link and scanner
checks remain documented in the [handoff](./HANDOFF.md).

## Toolchain & ThinLTO Linking Invariants

Compilation on the Ubuntu build server (`192.168.3.30`) with Clang 21.1.8 requires specific linker invariants:

1. **Single-threaded ThinLTO:** Multi-threaded linking (`--threads=4 --thinlto-jobs=4`) triggers an internal compiler crash (Segmentation fault in `llvm::MetadataTracking::untrack` via `StdThreadPool::processTasks` during LLVMContext destruction). Use `--threads=1 --thinlto-jobs=1`.
2. **Debug Info Stripping:** Full DWARF5 debug information combined with BTF causes relocation table overflow in `ld.lld`. Always build with `--enable DEBUG_INFO_NONE`, `--disable DEBUG_INFO_DWARF5`, and pass `LDFLAGS_vmlinux=--strip-debug`.
3. **Thin Archive Packaging:** `llvm-ar` segfaults when indexing certain ThinLTO LLVM bitcode object archives. Force GNU binutils archiver via `AR=/usr/bin/ar` to ensure reliable `.a` archive indexing.
4. **Build Host Specs:** The active host `192.168.3.30` is an Intel Core i5-2310 (4 cores / 4 threads, 16 GB DDR3). The higher-parallelism Xeon Gold 6138 workstation (`192.168.3.21`, 20c/40t, 64 GB DDR4 ECC) is present on LAN, but requires starting its WSL service before offloading builds.

## SPMI RF PMIC Driver Integration

The MediaTek MT6878 SPMI bus controls two companion RF PMICs:
- **MT6363** at slave address `0-04` (`mediatek,mt6363-rf-regulators`)
- **MT6369** at slave address `0-05` (`mediatek,mt6369-rf-regulators`)

In earlier builds, `run-radio-test.sh` stalled at `wait_for_suppliers` because these SPMI devices were unbound. The fix integrates the RF PMIC driver into upstream Linux 7.2:
- Driver source: `drivers/regulator/mt636x-rf-regulator.c`
- Configuration: `CONFIG_REGULATOR_MT636X_RF=y` and `CONFIG_REGMAP_SPMI=y`
- Live verification on Kernel Build #7 confirms both nodes bind to `mt636x-rf-regulator` under `/sys/bus/spmi/devices/0-04/driver` and `0-05/driver`.

## Kali NetHunter Rootfs Container Architecture (Method A)

The phone contains a 13 GB Kali NetHunter rootfs installed in Android's userdata partition (`/data/local/nhsystem/kali-arm64`).

### Android 16 FBE / Metadata Encryption Invariant
Android 16 enforces hardware-backed File-Based Encryption (FBE) and full metadata encryption on `/dev/sdc82` (`userdata`) using `dm-default-key` with `aes-xts-plain64`. The encryption keys are wrapped by the hardware Keymaster/TEE and reside in `/metadata/vold/metadata_encryption/`. Mainline Linux cannot read raw blocks of `/dev/sdc82` without TEE key derivation.

### Unencrypted Sparse Container (Method A)
To allow Kali NetHunter access without breaking Android 16:
1. Created an unencrypted 16 GB sparse ext4 image at `/data/local/kali-rootfs.img` (`truncate -s 16G` and `mke2fs -t ext4 -F -O ^has_journal`).
2. Resolved Android SEAndroid block loopback restriction (`I/O error` on `/dev/block/loop*`) by temporarily setting `setenforce 0`.
3. Mounted the container on `/mnt/kali_target` and fully replicated the 13 GB chroot tree via `cp -a /data/local/nhsystem/kali-arm64/* /mnt/kali_target/`.
4. Unmounted, detached the loop device, and restored SELinux to `Enforcing` (`setenforce 1`).
5. The container is fully preserved and self-contained; Android 16 and original NetHunter remain completely functional.

## Hardware Checkpoint: Kernel Build #7 Verification

Kernel Build #7 (`Linux 7.2.0-cmf-wifi-pmic+ #7 SMP PREEMPT Tue Nov 17 19:04:37 IST 2026`) booted successfully on physical hardware:
- **Display:** SimpleFB operational on `/dev/fb0` with 1080x2400 @ 32bpp (stride 4352).
- **Console:** Interactive root ACM shell on `/dev/cu.usbmodem01234567891` (`/dev/ttyGS0`).
- **SPMI:** Both MT6363 (`0-04`) and MT6369 (`0-05`) bound to `mt636x-rf-regulator`.
- **Connectivity Stack:** MediaTek `conninfra`, `connfem`, `connadp`, `mt6685_consys_regmap`, and `wmt_chrdev_wifi_connac2` loaded.
- **Wi-Fi Pre-Calibration Analysis:** `wlan_drv_gen4m_6878` power-on timed out at `pre_cal_flow: 0` after ConnInfra failed to bind and Bluetooth reported `btmtk_wcn_beif_init emi_ap_phy_base is NULL`. Build #8 restored the missing MT6878 connectivity power-domain driver match. See the [Build #8 hardware receipt](../evidence/build8-wifi-fix.md).
- **USB CDC-NCM Alternative:** The kernel natively supports `ncm.usb0` in the ConfigFS gadget for immediate 480 Mbps host IP connectivity.

