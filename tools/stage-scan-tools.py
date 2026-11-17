#!/usr/bin/env python3
"""Stage Alpine scan tools in an unpacked diagnostic RAM bundle."""
from pathlib import Path
import sys
import tarfile

FILES = {
    "iw-6.17-r0.apk": {"usr/sbin/iw": "tools/iw"},
    "libnl3-3.11.0-r0.apk": {
        "usr/lib/libnl-3.so.200.26.0": "tools/libnl-3.so.200",
        "usr/lib/libnl-genl-3.so.200.26.0": "tools/libnl-genl-3.so.200",
    },
    "musl-1.2.6-r3.apk": {"lib/ld-musl-aarch64.so.1": "tools/ld-musl-aarch64.so.1"},
    "wireless-regdb-2025.10.07-r0.apk": {
        "lib/firmware/regulatory.db": "firmware/regulatory.db",
        "lib/firmware/regulatory.db.p7s": "firmware/regulatory.db.p7s",
    },
}

WPA_FILES = {
    "wpa_supplicant-2.11-r4.apk": {
        "sbin/wpa_supplicant": "tools/wpa_supplicant",
        "sbin/wpa_cli": "tools/wpa_cli",
    },
    "libcrypto3-3.5.9-r0.apk": {"usr/lib/libcrypto.so.3": "tools/libcrypto.so.3"},
    "libssl3-3.5.9-r0.apk": {"usr/lib/libssl.so.3": "tools/libssl.so.3"},
    "dbus-libs-1.16.2-r2.apk": {"usr/lib/libdbus-1.so.3.38.3": "tools/libdbus-1.so.3"},
    "pcsc-lite-libs-2.4.0-r4.apk": {
        "usr/lib/libpcsclite.so.1": "tools/libpcsclite.so.1",
        "usr/lib/libpcsclite_real.so.1": "tools/libpcsclite_real.so.1",
    },
}

SSH_FILES = {
    "dropbear-2026.91-r0.apk": {
        "usr/sbin/dropbear": "tools/dropbear",
        "usr/bin/dropbearkey": "tools/dropbearkey",
    },
    "utmps-libs-0.1.3.3-r0.apk": {"usr/lib/libutmps.so.0.1.3.3": "tools/libutmps.so.0.1"},
    "zlib-1.4-r0.apk": {"usr/lib/libz.so.1.4": "tools/libz.so.1"},
    "skalibs-libs-2.15.0.0-r0.apk": {
        "usr/lib/libskarnet.so.2.15.0.0": "tools/libskarnet.so.2.15",
    },
}

if len(sys.argv) < 3 or set(sys.argv[3:]) - {"--wpa", "--ssh"}:
    sys.exit(f"Usage: {sys.argv[0]} APK_DIRECTORY UNPACKED_BUNDLE [--wpa] [--ssh]")

packages, bundle = map(Path, sys.argv[1:3])
if "--wpa" in sys.argv[3:]:
    FILES.update(WPA_FILES)
if "--ssh" in sys.argv[3:]:
    FILES.update(SSH_FILES)
for package, members in FILES.items():
    with tarfile.open(packages / package, "r:gz", ignore_zeros=True) as archive:
        for source, relative_target in members.items():
            target = bundle / relative_target
            target.parent.mkdir(parents=True, exist_ok=True)
            member = archive.getmember(source)
            if not member.isfile():
                sys.exit(f"Expected a regular file: {package}:{source}")
            with archive.extractfile(member) as payload:
                target.write_bytes(payload.read())
            target.chmod(0o755 if member.mode & 0o111 else 0o644)
