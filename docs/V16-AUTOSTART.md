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
kernel payload declared in the Android boot header. The profile is
covered by the signed AVB hash descriptor. The initramfs finds the
`boot_a` block device by its partition name, checks the header and profile
SHA-256, and reads and extracts the archive in RAM. Startup performs no
block-device writes.

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
