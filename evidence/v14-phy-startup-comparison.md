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
