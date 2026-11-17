# v15 WLAN scan and link test

The November 17 passive scan used the verified v15 boot image with the
same kernel, radio modules, and factory radio configuration as the v15
calibration test. The RAM bundle added Alpine AArch64 `iw` 6.17 and the
signed wireless regulatory database. Kernel logs recorded scan
completion across 38 channels and `iw` returned 9 BSS records.

| Band | BSS records |
| --- | ---: |
| 2.4 GHz | 5 |
| 5 GHz | 4 |

The phone returned to Android after the scan, and the restored slot-A
boot partition matched the rollback SHA-256. Raw scan logs and the
restore transcript are in local `cmf-mainline/evidence/v14-startup.8rk6Un/`.

## WPA association and SSH

The phone associated over WPA with `wpa_state=COMPLETED` and obtained an
IPv4 lease via DHCP. A ping bound to `wlan0` reached the DHCP gateway with
3 of 3 replies and 0% loss. The test left the supplicant and DHCP renewal
client running.

A temporary Dropbear server bound to the WLAN IPv4 address on port 2222.
Password login and TCP forwarding were disabled. The Mac pinned the host
key received over the USB diagnostic console, authenticated with its
existing Ed25519 key, and ran `uname -r` and `id` over SSH. They returned
`7.2.0-cmf-wifi-pmic+` and `uid=0(root)`. Mainline remains running for
the requested shell.

Local evidence is in `cmf-mainline/evidence/v14-startup.pNpjX9/`.
Use `mainline-link.log` for WPA, DHCP, and gateway ping,
`mainline-ssh.log` for the endpoint and host key, and
`ssh-verification.log` for the authenticated wireless session. The
[handoff](../docs/HANDOFF.md) records the local launcher and rollback path.
