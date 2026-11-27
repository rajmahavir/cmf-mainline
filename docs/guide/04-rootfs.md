# 4. Build the Ubuntu + Phosh root filesystem

The phone runs **Ubuntu 26.04 "resolute" arm64**, server-style (no desktop meta-package, no snaps), with **Phosh 0.54**, GNOME Calls/Chatty, NetworkManager, ModemManager, BlueZ, PipeWire and OpenSSH, plus this project's systemd units in `rootfs/overlay/`. You build it once on the host, put it on a microSD card (the rescue system), and in step 6 copy it to internal storage.

## 4.1 Base system

```bash
host$ sudo CMF_HOSTNAME=cmf-phone CMF_USER=<you> CMF_TZ=<Region/City> \
        rootfs/build-rootfs.sh ~/cmf-root
```

The script bootstraps with `mmdebstrap` (through `qemu-user-static`), applies `rootfs/overlay/`, writes `/etc/cmf-native-boot` (the marker `/init` looks for), creates your user as UID 1000 (Phosh runs as that user), enables the units below, and masks the sleep targets and `hwclock.service` (suspend is still being brought up and there is no RTC). `rootfs/packages-manual.txt` lists every package on our reference phone, if you want the same set of apps.

| Unit | Job |
|---|---|
| `cmf-wifi-calibrate` | loads the MediaTek connectivity modules in order, writes the Wi-Fi NVRAM, waits for calibration, powers Wi-Fi on (before NetworkManager) |
| `cmf-wlan-quiet` | lowers the Wi-Fi driver's log level (it logs ~80 lines/s by default) |
| `cmf-bluetooth` | starts the Bluetooth HCI bridge for the vendor BT driver |
| `cmf-modem` | starts the modem LK left loaded: FSM checks, NV data copies in RAM, `t2-ccci-fsd`/`t2-ccci-rpcd`, IMS/data provisioning with `t2-modemd` |
| `cmf-speechd` | `t2-speechd`, the call-audio bridge between the modem and ALSA |
| `cmf-cellular-profile` | creates the NetworkManager mobile-data profile from `/etc/default/cmf-modem` |
| `cmf-audio-mixer` | sets the speaker and microphone mixer paths |
| `cmf-kinibi` | optional: loads the Trustonic Kinibi TEE driver (only if `/etc/cmf-kinibi-enable` exists) |
| `cmf-native-ok` | clears the boot-attempt marker once `multi-user.target` is reached (crash guard, see step 6) |
| `cmf-emergency-serial` | keeps a root shell on the USB serial port in emergency/rescue mode |
| drop-ins | Phosh GPU environment, the ModemManager build, serial autologin, time-sync waits, a 20 s hardware watchdog |

**Carrier settings:** edit `/etc/default/cmf-modem` (data APN, IMS APN) and the APN in `/etc/udev/rules.d/77-cmf-mm-ccci.rules`. The defaults are for Reliance Jio (India), the only carrier this port has been tested on.

## 4.2 Hardware components

These come from other sources and go into the tree before the first boot.

### Firmware (from your phone, step 1.4)
```bash
host$ sudo mkdir -p ~/cmf-root/lib/firmware
host$ for f in soc7_0_ram_mcu_1_1_hdr.bin soc7_0_ram_mcu_1a_1_hdr.bin soc7_0_ram_mcu_1b_1_hdr.bin \
               soc7_0_ram_bt_1_1_hdr.bin soc7_0_ram_bt_1a_1_hdr.bin soc7_0_ram_bt_1b_1_hdr.bin \
               BT_FW.cfg wifi.cfg conninfra.cfg connfem.cfg; do
        sudo tar -xf backup/vendor-files/vendor-firmware.tar -O firmware/$f > /tmp/$f &&
        sudo install -m 0644 /tmp/$f ~/cmf-root/lib/firmware/$f
      done
host$ sudo install -m 0644 backup/vendor-files/WIFI.nvram ~/cmf-root/lib/firmware/cmf-wifi-nvram.bin
```
`connfem.cfg` may be empty; that is expected. `regulatory.db` is installed by the `wireless-regdb` package.

### Wi-Fi and Bluetooth modules
Get Nothing's CMF Phone 1 kernel-module source release (`kernel_device_modules-6.1`), check its fingerprint and apply the patches in `drivers-oot/connectivity/` (`PROVENANCE.md`, `APPLY-STATUS.md`, `scripts/apply-patches.sh`), build the connectivity stack against your kernel's `out/` tree (`wifi-bt/rebuild-wlan.sh`, `wlan-dependency-order.txt`), then install the eleven modules to `/usr/local/lib/cmf-wifi/7.2.0-cmf-wifi-pmic+/` (`cmac`, `rfkill`, `cfg80211`, `connadp`, `connfem`, `conninfra`, `wmt_chrdev_wifi_connac2`, `wlan_drv_gen4m_6878`, `bluetooth`, `bt_drv_6878`, `mt6685-consys-regmap`). They are deliberately **outside `/lib/modules`**: udev must not autoload them before their power suppliers are ready, because the vendor probe only runs once. `cmf-wifi-calibrate` loads them in the right order.

### Modem helpers (Serlk's titan2-tools)
`t2-speechd`, `t2-modemd`, `t2-ccci-fsd` and `t2-ccci-rpcd` are from [Serlk/titan2-tools](https://github.com/Serlk/titan2-tools) (GPL-2.0-or-later), used unchanged. Build them on the phone or with an aarch64 cross compiler (each has a Makefile under `tools/`), and install them to `/usr/local/lib/cmf-modem/`:
```bash
phone# git clone https://github.com/Serlk/titan2-tools && cd titan2-tools
phone# make -C tools/speechd && make -C tools/modemd && make -C tools/ccci-fsd && make -C tools/ccci-rpcd
phone# install -m 0755 tools/speechd/t2-speechd tools/modemd/t2-modemd \
         tools/ccci-fsd/t2-ccci-fsd tools/ccci-rpcd/t2-ccci-rpcd /usr/local/lib/cmf-modem/
```

### ModemManager with the mtk-ccci plugin
Ubuntu's ModemManager has no driver for the CCCI modem. Build ModemManager with the `mtk-ccci` plugin from `titan2-tools/tools/mm-titan2/` (`apply.sh`, `build-on-phone.sh`; see its `INSTALL.md`) and install it to `/opt/cmf-mm-ccci-build/` so that `/opt/cmf-mm-ccci-build/out/src/ModemManager` exists. The drop-in `ModemManager.service.d/cmf.conf` runs that binary in place of the stock one.

### Mesa (GPU)
Phosh renders on the Mali-G615 through Panthor. We use **Mesa 26.3** with the five Panfrost/PanVK fixes in `titan2-tools/patches/mesa/`, installed to `/opt/cmf-mesa-26.3` (the Phosh drop-in sets `LD_LIBRARY_PATH` to it):
```bash
phone# meson setup build -Dprefix=/opt/cmf-mesa-26.3 -Dgallium-drivers=panfrost \
         -Dvulkan-drivers=panfrost -Dplatforms=wayland -Dglx=disabled -Dbuildtype=release
phone# ninja -C build install
```
Ubuntu's own Mesa may also work; we have not tested it. If you try it, remove `LD_LIBRARY_PATH` from `/etc/systemd/system/phosh.service.d/cmf.conf`.

### Optional
- **Kinibi TEE**: build `drivers-oot/kinibi-mobicore` against your `out/` tree, install it as `cmf-kinibi-boot` expects (`/usr/local/lib/cmf-kinibi/<release>/`), copy the vendor daemon and trustlets from your phone, then `touch /etc/cmf-kinibi-enable`.
- **Tailscale** for remote SSH; **off-device crash logging**: `tools/crashlog/`.

## 4.3 Write the microSD card

```bash
host$ sudo parted /dev/sdX --script mklabel gpt mkpart cmf-sd ext4 1MiB 100%
host$ sudo mkfs.ext4 -L cmf-sd /dev/sdX1
host$ sudo mount /dev/sdX1 /mnt && sudo cp -a ~/cmf-root/. /mnt/ && sudo umount /mnt
```

Add your SSH public key to `/home/<you>/.ssh/authorized_keys` on the card if you want SSH from the first boot.

Next: [5. First boot](05-first-boot.md).
