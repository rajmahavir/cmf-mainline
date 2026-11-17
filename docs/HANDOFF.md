# CMF Phone 1 mainline handoff

## Resume from v15

The FEM-only v15 diagnostic passed Bluetooth and Wi-Fi pre-calibration and
created `phy0` and `wlan0` on hardware. The test then restored Android.
The next milestone is to run an `iw` scan, then verify association and IP
traffic on mainline. Start from the same v15 boot image and RAM bundle.
The [hardware comparison and result](../evidence/v14-phy-startup-comparison.md)
records why the FEM subtree was removed and which changes were tested.

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
