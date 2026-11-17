# CMF Phone 1 mainline handoff

## Resume from the live Build #8 Wi-Fi shell

The phone is on slot A with kernel `7.2.0-cmf-wifi-pmic+ #8`. Its
MT6878 connectivity power controller and ConnInfra drivers bind, both
radio calibration callbacks pass, and the boot profile starts Wi-Fi and
key-only SSH automatically. Use the local
`cmf-mainline/lab/v15-link/SSH to CMF.command` launcher to reconnect.
The [Build #8 hardware receipt](../evidence/build8-wifi-fix.md) records
the failure that was fixed and the verification evidence.

Build #8 uses the same device tree and private profile layout as Build #7.
The profile is locally personalized. Its Wi-Fi credential and SSH host
key stay in the private image and on the phone. The local artifact
directory is `cmf-mainline/lab/build7-wifi-fix/`. Its
`private-path.txt` points to the signed image's parent directory on
the Mac. The Ubuntu result is at
`~/cmf-template-fast/conn-domain-fix.tTXm2T/Image`. The Android rollback
remains `cmf-mainline/rollback/slot-a-current/boot_a.img`.

The [Build #7 receipt](../evidence/v16-hardware-rf-pmic-receipt.md) covers
the display, RF PMIC integration, UFS enumeration, and Android-side Kali
rootfs export.

## Prior Milestone: v15 Wi-Fi Shell

v15 passed both calibration callbacks, passive scanning, WPA association,
DHCP, gateway ping, and key-authenticated root SSH from the Mac. The phone
was deliberately left running the v15 initramfs on slot A for shell access.
For the live endpoint and pinned host key, use the local launcher
`cmf-mainline/lab/v15-link/SSH to CMF.command`. Confirm the device state
before starting a new capture. The rollback image remains in the local
workspace. Read the [scan and link evidence](../evidence/v15-wlan-link.md)
and the [FEM comparison](../evidence/v14-phy-startup-comparison.md).

## Local workspace

On the Mac used for this test, the project directory is
`~/My Drive/Codex/CMF`. The Git checkout lives in `cmf-mainline-git/`.
The device artifacts live in the sibling `cmf-mainline/` directory.

| Local path relative to `cmf-mainline/` | Contents |
| --- | --- |
| `images/diagnostic-v14-mt6685-boot.img` | Audited v14 boot image, SHA-256 `6c02de9a05403e0675a131c44d4bb1b6b92b724441df93e7656ee88ebb80dff7` |
| `rollback/slot-a-current/boot_a.img` | Android rollback, SHA-256 `eda9f9a5e32dcaf478e8464a473cfb0d1e447c52d97b2e76d1036ff4d7b370fb` |
| `lab/v14-startup/` | Signed MT6685 module, reboot helper, and assembled RAM test bundle |
| `evidence/v14-startup.*/` | Per-run Android and mainline logs, Fastboot transcript, and Android restore result |
| `docs/ROLLBACK.md` | Slot-A recovery procedure and manual bootloader entry |

The Ubuntu build tree used for v14 is
`~/mt6878-mainline/out/wifi-port-probe.Kl8geuUh`. Its
`rf-provider.zkcie89P` build output contains the v13/v14 kernel and symbols.
The rebuilt boot image and module are in `v14-mt6685.k5a9fx/image/` under
that tree. Source for the [clock provider](../wifi-bt/mt6685/)
and [device-tree addition](../dts/mt6878-tetris-mt6685-consys.dtsi) is in
GitHub. Keep factory NVRAM, firmware, and raw driver dumps in local
storage. Publish source, checksums, and selected diagnostic lines.

## Reproduce the capture

With the project directory as the working directory, set the phone serial from
`adb devices -l` and the mainline USB serial path observed under `/dev`.

```sh
export CMF_WORKSPACE="$PWD/cmf-mainline"
export CMF_CAPTURE_ASSETS="$CMF_WORKSPACE/lab/v14-startup"
export CMF_DEVICE_SERIAL='<adb serial>'
export CMF_SERIAL_PORT='<diagnostic USB serial path>'
bash cmf-mainline-git/tools/capture-v14.sh
```

The script verifies both images and the current on-device `boot_a` before
flashing. It records the output directory at startup. A nonzero
calibration result still triggers full ring capture and rollback. Check
`v14-mt6685-android-restore.log` for the Android boot and hash read-back
before another test. If the serial recovery path cannot reach Fastboot,
use the manual bootloader mode from the local rollback document, confirm
slot A and `is-userspace: no`, then restore the verified rollback image
to `boot_a`. All hardware trials use slot A and write only `boot_a`.

## Continuous trace checkpoint

The full v14 run is saved under `evidence/v14-startup.aAi3Ys/`.
`android-dmesg.txt` and `mainline-startup.kmsg` contain the Android and
mainline startup sequences. The [curated PHY comparison](../evidence/v14-phy-startup-comparison.md)
identifies the first failing firmware exchange and the FEM-only follow-up.
The v14 capture verified rollback to Android after collecting the trace.

The v15 FEM-only image is at
`images/diagnostic-v15-android-fem-boot.img` in the local artifact directory.
Its SHA-256 is
`f3124e12765d188dfa7c3f4f3be3ec4769529fec7b271c17b30400b45212e528`.
The build server's v15 output is `v15-fem.4i5S7q/image/` under the same
build tree. To capture v15 with the existing bundle, set
`CMF_DIAGNOSTIC_IMAGE` to the image path and `CMF_DIAGNOSTIC_SHA256` to its
hash before running the capture command above. Set `CMF_FEM_MODE=unbound`
for this image. The ported ConnFEM module loads and exports its symbols,
but the platform probe returns `-95` when the FEM data subtree is absent.
The original preparation check expected a bound device and stopped the
first v15 attempt before WLAN module load. The device-tree diff
removes the FEM subtree and the ten part-node phandles it referenced.
The v14 kernel and signed modules are reused.

The successful v15 run is under `evidence/v14-startup.6eoNvH/` in the local
artifact directory. Inspect `mainline-startup.kmsg` for the two PHY responses
and calibration summary, `mainline-power.log` for interface enumeration, and
`v14-mt6685-android-restore.log` for the rollback hash verification. The capture
directory prefix and some log names retain `v14` for compatibility with the
capture script. The candidate hash and v15 boot image identify the run.

## Reproduce WLAN and SSH testing

For passive scanning, use `CMF_CAPTURE_ASSETS="$CMF_WORKSPACE/lab/v15-scan"`,
`CMF_TEST_SCAN=1`, and the v15 image and FEM settings above. For association,
use the bundle in `lab/v15-link/` and set `CMF_WPA_CONFIG` to a private,
local file named `wpa.conf` containing a single authorized test network and
`ctrl_interface=/tmp/cmf-wpa`. The capture uploads it into RAM and removes
it after the link check. Keep credentials out of the repo and the reusable
bundle.

To start SSH, set `CMF_SSH_PUBLIC_KEY` to a local file named
`cmf_authorized_keys` containing the authorized OpenSSH public key.
Set `CMF_KEEP_LINK=1` to leave a successful session running. Without that
setting, the capture restores Android. The phone generates an ephemeral
host key in RAM and starts Dropbear on the WLAN IPv4 address, port 2222.
The capture reports `SSH_ADDRESS` and `SSH_HOST_KEY` through USB so the client
can pin the key before connecting. WPA and DHCP renewal stay running.
Restarting the phone ends this live session.

`tools/stage-scan-tools.py` stages the pinned Alpine AArch64 package
files into an unpacked RAM bundle. Add `--wpa --ssh` for the full setup.
The downloaded packages and their hashes are in local
`lab/v14-startup/scan-packages/`. The raw scan and SSH verification traces
are referenced by the [test evidence](../evidence/v15-wlan-link.md).
