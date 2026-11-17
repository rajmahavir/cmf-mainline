# CMF Phone 1 radio bring-up

## v13 hardware retest on November 17, 2026

The v13 image booted on slot A and provided a working USB serial console.
All ten module hashes passed verification. The connectivity drivers bound,
and the Wi-Fi driver accepted the factory NVRAM through `/dev/wmtWifi`.
The Wi-Fi and Bluetooth drivers registered their calibration callbacks.

The first failure occurred when connectivity power-on tried to select a
clock scheme. Calibration stopped before the Bluetooth and Wi-Fi routines ran.

```text
[pre_cal] trigger pre-cal BT/WF are registered
consys_get_co_clock_type_mt6878, failed to get regmap.
[consys_hw_pwr_on] Cannot get clock type, ret=-1
[opfunc_power_on_internal] Conninfra power on fail. drv(0) ret=(-4369)
BT power on fail during pre_cal
```

The Android `boot_a` image was restored. Android reported
`sys.boot_completed=1` on slot A, and the partition read-back matched
the saved image's SHA-256.

## v14 clock regmap

The stock tree puts `mediatek,mt6685-consys` under the MT6685 at SPMI slave
9. The vendor connectivity driver's clock probe takes the regmap from
that parent. The v13 diagnostic omitted both nodes.

The v14 boot image adds the MT6685 parent and connectivity child using
[`mt6878-tetris-mt6685-consys.dtsi`](../dts/mt6878-tetris-mt6685-consys.dtsi).
It keeps the v13 kernel binary. The
[diagnostic module](../wifi-bt/mt6685/mt6685-consys-regmap.c) binds the parent,
checks that it can read the DCXO clock strap at `0x7f4`, and registers the
child for the existing connection driver. The regmap exposes that register
for reads only.

The module built against the v13 kernel with matching vermagic and a
SHA-512 module signature. The v14 image passed AVB and boot wrapper checks.
The DTB diff against v13 contained only the two new nodes and their
properties. The boot image SHA-256 is
`6c02de9a05403e0675a131c44d4bb1b6b92b724441df93e7656ee88ebb80dff7`.

Use [`run-radio-test.sh`](../tools/run-radio-test.sh) in the extracted RAM
bundle. `prepare` verifies module hashes, stages local firmware, and checks
provider binding. `calibrate` submits the factory NVRAM and checks both
calibration results. `power` checks for the Wi-Fi interface after power-on.

## v14 hardware result on November 17, 2026

The MT6685 provider bound to SPMI device `0-09` and read DCXO strap bit
`0`. The first attempt loaded the vendor connectivity module at about
4.7 seconds and its one-shot probe returned `-19`. Loading after the
RF PMICs were bound and kernel uptime reached 15 seconds let the
same v14 image bind connectivity and enter calibration.

Bluetooth calibration returned `0`. Wi-Fi reached firmware startup, then
asserted in `wifi/base/soc7_0/hal/phy/6631/hal_phy_rom.c:1361`.
The Wi-Fi calibration callback returned `-1`, so the test stopped before
enabling the interface or scanning.

```text
mt6685-consys-regmap 0-09: MT6685 DCXO strap bit: 0
wlanAdapterStart:(INIT WARN) Fail reason: 5
wlan_precal_docal_v1:(INIT ERROR) failed, ret=-1
[pre_cal] summary pwr=[705] bt_cal=[0][14] wf_cal=[-1][336] backup=[0]
```

The vendor's per-driver text labels `ret=[-1]` as success. The test now
checks the numeric return codes in the summary and accepts only `0` and
`2`, the documented pass values.

The phone returned to Android on slot A. The saved and on-device `boot_a`
SHA-256 digests matched after the restore.
