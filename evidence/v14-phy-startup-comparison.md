# v14 first WLAN PHY command comparison

The November 17 continuous capture preserved the startup messages emitted
before the firmware crash dump. Raw logs are in the local workspace
`cmf-mainline/evidence/v14-startup.aAi3Ys/`. The test restored Android
on slot A and verified the on-device rollback image SHA-256.

Android and mainline both select the 26 MHz co-clock, identify Wi-Fi chip
7961 E1, load NVRAM versions `0x107` and `0x7`, and align the same
166-byte PHY NVRAM region to 168 bytes. Both first send PHY action tag 2,
command 0 in a 588-byte init command with ID `0x08`.

| Observation | Android | Mainline v14 |
| --- | --- | --- |
| ConnFEM probe | Bound. Logs `Missing epa elna node` | Bound. Parses `epa-elna-mtk` and applies its pinmux |
| FEM supplied to PHY | Driver reports FEM unavailable | ID `0x00220407`, WLAN7207H/WLAN7207C, 15 pins |
| Init time-sync command before PHY | Sends `0x09` before `0x08` | No preceding `0x09` |
| First PHY response | `HAL_PHY_ACTION_TAG_NVRAM status[0x0]` | Firmware assert about 77 ms after transmit, then host RX wait fails |
| Pre-calibration summary | BT `0`, Wi-Fi `0` | BT `0`, Wi-Fi `-1` |

The first controlled follow-up removes only the `epa-elna-mtk` subtree
from the v14 DTB using
[`mt6878-tetris-android-fem.dtsi`](../dts/mt6878-tetris-android-fem.dtsi).
The `connfem` device stays enabled and the same signed module provides its
symbols. This tests the hypothesis that the diagnostic's extra FEM data
or its pinmux causes the assertion. The stock missing-data path and the
time-sync difference were observed independently. Test the FEM change
before changing command `0x09`.

The time-sync difference has a separate test that needs no driver rebuild.
The existing `kalSyncTimeToFW()` uses wall time and skips the command if
the adapter's last update plus 7,200 seconds is later than the current
wall clock. The diagnostic initramfs starts its clock at the Unix epoch.
If the FEM-only test still asserts, try setting the running initramfs
clock to the host UTC epoch immediately before loading the WLAN module.
Verify whether `kalSyncTimeToFW` and command `0x09` then appear in the trace.

## v15 FEM-only hardware result

Removing `epa-elna-mtk` left the same ConnFEM module loaded without a bound
FEM provider. The diagnostic kept the v14 kernel, firmware, NVRAM and module
bundle. The DTB audit found only the removed FEM data properties and
ten now-unreferenced part phandles. The boot image passed AVB and wrapper
verification.

The first PHY NVRAM command returned status `0x0`. The calibration command
returned event `0x81`, status `0x0`, and the same EMI result offset and length
as Android. The pre-calibration summary reported `bt_cal=[0]` and
`wf_cal=[0]`. The power-on check then found `phy0` and `wlan0`.
No host clock update or WLAN driver change was applied.

```text
HAL_PHY_ACTION_TAG_NVRAM status[0x0]
HAL_PHY_ACTION_TAG_CAL ucEvent[0x81]status[0x0]emiAddr[0xf0b701a4]emiLen[0x2b000]
[pre_cal] summary pwr=[693] bt_cal=[0][19] wf_cal=[0][2998] backup=[0]
phy0
lo     p2p0   p2p1   sit0   wlan0
```

This isolates the assert to the presence of the extra FEM configuration
or its pinmux in the v14 diagnostic. Scan, association, IP traffic, and
Bluetooth traffic still need separate verification. The v15 raw traces and
boot image location are recorded in the [handoff](../docs/HANDOFF.md).
