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
   kernel `Image` with an uncompressed initramfs and a 12 MiB placeholder.
   The template has no per-device radio files, network profile, or keys.
2. On the Mac, unpack the verified local v15 link bundle into a private
   directory. Update `run-radio-test.sh` from the checkout and add the
   local `reboot-bootloader` helper. Include a private `wpa.conf`, the
   OpenSSH public key as `cmf_authorized_keys`, and a Dropbear Ed25519
   host key as `cmf_dropbear_host_key`. The host key stays the same across
   reboots of this image. Create a gzip tar archive of the bundle.
3. Run `python3 boot/embed-autostart-bundle.py TEMPLATE_IMAGE BUNDLE_TGZ IMAGE`.
   It requires one intact placeholder and preserves the kernel layout.
4. Run `wifi-bt/package-wifi-pmic.py` with the verified v9 boot image,
   personalized kernel, v15 DTB, packaging-tools directory, and private
   output directory. Use `--avb-key` with the existing AVB public key and
   `--signing-helper` to delegate signing to the Ubuntu host. Only the
   AVB signing block goes to Ubuntu. The packager verifies the signed
   image and checks that its payload matches the local inputs.

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
