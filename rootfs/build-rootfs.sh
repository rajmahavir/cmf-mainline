#!/bin/bash
# Build the Ubuntu 26.04 + Phosh root filesystem for the CMF Phone 1 on an
# x86-64 host (mmdebstrap + qemu-user-static), then apply the CMF overlay.
#
# usage: sudo rootfs/build-rootfs.sh TARGET_DIR
#   env: CMF_HOSTNAME (default cmf-phone), CMF_USER (default phone),
#        CMF_TZ (default Etc/UTC), CMF_LOCALE (default en_US.UTF-8)
#
# The result is a directory tree. Copy it to an ext4 microSD partition
# (docs/guide/04-rootfs.md), then add the hardware components listed there
# (firmware, Wi-Fi modules, modem helpers, ModemManager build, Mesa).
set -euo pipefail

T=${1:?usage: sudo $0 TARGET_DIR}
REPO=$(cd "$(dirname "$0")/.." && pwd)
HOSTNAME_=${CMF_HOSTNAME:-cmf-phone}
USER_=${CMF_USER:-phone}
TZ_=${CMF_TZ:-Etc/UTC}
LOCALE_=${CMF_LOCALE:-en_US.UTF-8}
[ "$(id -u)" = 0 ] || { echo "run as root (mmdebstrap needs it)"; exit 1; }
[ ! -e "$T" ] || { echo "$T already exists"; exit 1; }

PK=systemd,systemd-sysv,systemd-timesyncd,udev,dbus,dbus-user-session,login,sudo
PK=$PK,locales,tzdata,ca-certificates,openssh-server,iproute2,iputils-ping,curl
PK=$PK,nano,less,kmod,e2fsprogs,dmsetup,util-linux,procps,psmisc,busybox,python3
PK=$PK,python3-gi,network-manager,modemmanager,wpasupplicant,wireless-regdb,bluez
PK=$PK,pipewire,pipewire-pulse,wireplumber,alsa-utils,phosh,phoc,phosh-osk-stevia
PK=$PK,phosh-mobile-settings,seatd,gnome-session-bin,gnome-session-common
PK=$PK,gnome-settings-daemon,gnome-settings-daemon-common,gnome-backgrounds
PK=$PK,gnome-control-center,gnome-keyring,at-spi2-core,evolution-data-server
PK=$PK,gnome-calls,chatty,gnome-console,ptyxis,nautilus,epiphany-browser
PK=$PK,fonts-noto-core,adwaita-icon-theme,mesa-utils,libgl1-mesa-dri,libvulkan1
PK=$PK,iw,rfkill,usbutils,zstd,xz-utils,ubuntu-keyring

M=http://ports.ubuntu.com/ubuntu-ports
mmdebstrap --arch=arm64 --variant=apt --components=main,universe \
    --include="$PK" \
    --aptopt='APT::Install-Recommends "false"' \
    --customize-hook="echo $HOSTNAME_ > \"\$1/etc/hostname\"" \
    --customize-hook="printf '127.0.0.1 localhost\n127.0.1.1 $HOSTNAME_\n' > \"\$1/etc/hosts\"" \
    --customize-hook="ln -sf /usr/share/zoneinfo/$TZ_ \"\$1/etc/localtime\"; echo $TZ_ > \"\$1/etc/timezone\"" \
    --customize-hook="chroot \"\$1\" sh -c 'sed -i \"s/^# *$LOCALE_/$LOCALE_/\" /etc/locale.gen && locale-gen >/dev/null && echo LANG=$LOCALE_ > /etc/default/locale'" \
    resolute "$T" \
    "deb $M resolute main universe" \
    "deb $M resolute-updates main universe" \
    "deb $M resolute-security main universe"

# CMF overlay: units, scripts, udev rules, settings
cp -a "$REPO/rootfs/overlay/." "$T/"
chmod 0755 "$T"/usr/local/sbin/* "$T"/opt/cmf-bluetooth/*.sh "$T"/opt/cmf-bluetooth/*.py
mkdir -p "$T/usr/local/lib/cmf-modem" "$T/usr/local/lib/cmf-wifi" "$T/var/lib/cmf-native"

# Native boot marker: /init switch_roots into roots that have it
: > "$T/etc/cmf-native-boot"
# The SD root is found by /init; systemd only needs to remount it.
echo 'LABEL=cmf-sd / ext4 defaults,noatime,errors=remount-ro 0 1' > "$T/etc/fstab"

# User (UID 1000 runs Phosh)
chroot "$T" useradd -m -u 1000 -s /bin/bash -G sudo,video,audio,render,input,dialout "$USER_"
echo "Set a password for $USER_ (used for the Phosh lock screen and sudo):"
chroot "$T" passwd "$USER_"

# Services
chroot "$T" systemctl enable phosh.service serial-getty@ttyGS0.service \
    systemd-timesyncd.service NetworkManager.service ModemManager.service bluetooth.service \
    cmf-native-ok.service cmf-emergency-serial.service cmf-wifi-calibrate.service \
    cmf-wlan-quiet.service cmf-bluetooth.service cmf-audio-mixer.service \
    cmf-modem.service cmf-speechd.service cmf-cellular-profile.service cmf-kinibi.service
chroot "$T" systemctl set-default graphical.target
# Suspend is still being brought up and the phone has no RTC.
chroot "$T" systemctl mask sleep.target suspend.target hibernate.target \
    hybrid-sleep.target hwclock.service

echo
echo "Root filesystem ready in $T ($(du -sh --one-file-system "$T" | cut -f1))."
echo "Next: add firmware, Wi-Fi modules, modem helpers, ModemManager and Mesa (docs/guide/04-rootfs.md)."
