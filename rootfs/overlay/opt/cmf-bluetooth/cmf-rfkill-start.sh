#!/bin/sh
set -eu
if [ ! -c /dev/rfkill ]; then
    IFS=: read -r major minor < /sys/class/misc/rfkill/dev
    mknod -m 0600 /dev/rfkill c "$major" "$minor"
fi
pkg=/opt/cmf-bluetooth/rfkill-package
export LD_LIBRARY_PATH="$pkg/usr/lib/gnome-settings-daemon-51${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$pkg/usr/libexec/gsd-rfkill"
