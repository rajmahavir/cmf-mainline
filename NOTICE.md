# Notice: credits, origins and licensing

Copyright of each part stays with its author. Everything written for this project is **GPL-2.0-only** (see [LICENSE](LICENSE)).

## Where the code comes from

| Content | Origin | License |
|---|---|---|
| Kernel base | [MT6878-Mainline/linux](https://github.com/MT6878-Mainline/linux), commit `12f2e9f04` (postmarketOS v7.2 line) | GPL-2.0 (upstream copyrights retained) |
| `kernel/patches/0012` (CCCI modem), `0013` (MT6878 audio) and parts of others | largely from **Serlk's [titan2-linux](https://github.com/Serlk/titan2-linux)** (Unihertz Titan 2, same MT6878 SoC) and MediaTek vendor code, adapted for the CMF Phone 1 | GPL-2.0; per-file copyright headers (Serlk, MediaTek) kept inside the patches |
| Rest of `kernel/patches/`, `kernel/dts/` | this project, building on MT6878-Mainline and MediaTek code | GPL-2.0 |
| `drivers-oot/connectivity/` | **diffs** against Nothing's published CMF Phone 1 kernel-module sources (`kernel_device_modules-6.1`, MediaTek connectivity); the original sources are **not** redistributed. `PROVENANCE.md` fingerprints the exact tree | GPL-2.0 |
| `drivers-oot/kinibi-mobicore/` | Trustonic MobiCore/Kinibi kernel driver (as published in the vendor kernel sources) | GPL-2.0 |
| `boot/`, `initramfs/`, `scripts/`, `rootfs/`, `tools/`, `drivers-oot/disp-iommu*`, docs | written for this project | GPL-2.0-only |
| Modem helpers `t2-speechd`, `t2-modemd`, `t2-ccci-fsd`, `t2-ccci-rpcd`; ModemManager `mtk-ccci` plugin; Mesa patches | **Serlk's [titan2-tools](https://github.com/Serlk/titan2-tools)**, used unchanged; built from that repository, not copied here | GPL-2.0-or-later |
| Firmware, NVRAM/calibration data, Kinibi trustlets, Mali CSF firmware | MediaTek / Nothing / Arm / module vendors | **not included**; copied from your own phone (or linux-firmware for Mali) |

## Thanks

- **MT6878-Mainline** for the mainline MT6878 kernel base, lplk and u-boot work.
- **Serlk** for the Unihertz Titan 2 port: the CCCI modem and audio drivers, the `t2-*` modem userspace, the ModemManager plugin and the LK documentation in [titan2-docs](https://github.com/Serlk/titan2-docs). Much of this port's phone functionality exists because that work was published.
- **MediaTek** and **Nothing** for the GPL source releases.
- **postmarketOS**, **Phosh**, **Ubuntu**, **Mesa/Panfrost/Panthor** and upstream Linux developers.
- [bkerler/mtkclient](https://github.com/bkerler/mtkclient) and [bengris32/liblk](https://github.com/bengris32/liblk) for tooling used during bring-up.

## Rules for contributions

- Never add firmware, NVRAM or calibration data, partition dumps, IMEI/serial-bearing logs, keys, or personal network details.
- Never add files from NDA-only releases. Connectivity work must stay traceable to the public GPL release.
- Keep vendor and third-party copyright headers intact inside patches.

## Disclaimer

This is an unsupported community port. Images built from these instructions can fail to boot; the recovery path is fastboot on `boot_a` with your saved stock image. Not affiliated with or endorsed by Nothing Technology, MediaTek, Arm, Trustonic or Unihertz.
