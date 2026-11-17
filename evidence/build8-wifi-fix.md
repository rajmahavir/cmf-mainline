# Build #8 restores automatic Wi-Fi

On November 17, 2026, Build #7 booted on the phone but ConnInfra's
platform probe returned `-110`. The kernel had MediaTek power-domain
support enabled and the live device tree contained the EMI reservation.
The Build #7 source tree lacked the driver match for
`mediatek,mt6878-power-controller`. Its connectivity power-domain
supplier stayed unbound, so ConnInfra never initialized the EMI state
used by the Wi-Fi and Bluetooth drivers.

Build #8 applies the [MT6878 connectivity power-domain patch](../patches/mt6878-connectivity-power-domain.patch)
from the working v15 build. The template builder checks for the match
before rebuilding. The radio startup script waits for the connectivity
power controller and both RF PMICs before loading ConnInfra, whose
probe is a one-shot attempt.

The repaired kernel retains Build #7's device tree, signed Wi-Fi modules,
and private-profile layout. The locally packaged image passed AVB
verification. Only `boot_a` was flashed.

| Artifact | SHA-256 |
| --- | --- |
| Build #8 Linux Image | `3c95c484f1237748b4b2dc03357690c619c4b9e7d3c9431d281e43338a83cce0` |
| Build #7 device tree reused by Build #8 | `20411eb84694d421a80f5405bdef7655931a7fafe1cd74495cba3e2bc5ab7254` |
| Personalized Build #8 boot image | `38dec5fa4d44b205d60d192fef7ea9c53d4696487d50e23c04856e25fbc133b7` |

The first boot bound `mtk-power-controller` and `mtk_conninfra`. Both
Bluetooth and Wi-Fi pre-calibration returned `0`. `wlan0` appeared and
the Mac authenticated to SSH over Wi-Fi 44 seconds after Fastboot reboot.
No USB setup commands were required.

Local logs are in `cmf-mainline/evidence/build7-wifi-fix.9aW7r9/`.

A second run used a normal Linux restart issued over SSH. A different
kernel boot ID confirmed the restart. The Mac authenticated over Wi-Fi
again after 47 seconds. The DHCP renewal client was running and all five
gateway pings replied. Build #8 was left running with Wi-Fi and SSH active.

At the later November 17 pack-up, the saved Android image was restored
to `boot_a`. Android 16 completed boot on slot A, Wi-Fi reconnected, and
the partition read-back matched the saved image. See the
[handoff](../docs/HANDOFF.md) for the current device state.
