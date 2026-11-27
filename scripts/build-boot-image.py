#!/usr/bin/env python3
"""Assemble and sign a CMF Phone 1 boot image (boot_a) from a kernel Image and DTB.

usage: build-boot-image.py --image Image --dtb cmf-phone-1.dtb --key avb.pem \
                           --aosp-tools DIR --out boot.img [--profile profile.tar.gz]

Layout of the kernel payload (gzip-compressed, as LK expects):
  0x000000  shim header (boot/prebuilt/shim-head.bin): an arm64 Image header whose
            code passes the embedded DTB to the kernel and jumps to +2 MiB
  0x001000  DTB (must end before 0x10000)
  0x010000  LK modem-record helper (boot/prebuilt/lk-modem.bin, source in
            boot/lk-modem-diag/) that forwards LK's modem record to the kernel DTB
  0x200000  the kernel Image (with its built-in initramfs)

The boot image is header v4, signed with an AVB hash footer for the 64 MiB boot
partition. An unlocked bootloader accepts any key; generate your own with
`openssl genrsa -out avb.pem 2048`. --profile appends the optional legacy rescue
profile (CMF_BOOT_PROFILE_V1 at 32 MiB, read by initramfs/rootfs/cmf/autostart.sh).

DIR must contain mkbootimg.py and avbtool.py from AOSP
(platform/system/tools/mkbootimg and platform/external/avb).
"""
import argparse
import gzip
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DTB_OFFSET, LK_OFFSET, IMAGE_OFFSET = 0x1000, 0x10000, 0x200000
PARTITION_SIZE = 64 * 1024 * 1024
PROFILE_OFFSET, PROFILE_HEADER = 32 * 1024 * 1024, 4096
# AVB properties copied from the stock CMF Phone 1 boot image (Android 14 build).
FINGERPRINT = ("alps/hal_mgvi_64_64only_ww_armv82/mgvi_64_64only_ww_armv82:"
               "14/UP1A.231005.007/2609281140:user/release-keys")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True, type=Path)
    ap.add_argument("--dtb", required=True, type=Path)
    ap.add_argument("--key", required=True, type=Path)
    ap.add_argument("--aosp-tools", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--profile", type=Path)
    a = ap.parse_args()

    image = a.image.read_bytes()
    _, effective_size, flags = struct.unpack_from("<QQQ", image, 8)
    assert image[56:60] == b"ARMd", "not an arm64 Image"
    dtb = a.dtb.read_bytes()
    magic, dtb_size = struct.unpack_from(">II", dtb)
    assert magic == 0xD00DFEED and dtb_size == len(dtb), "bad DTB"
    assert DTB_OFFSET + dtb_size <= LK_OFFSET, "DTB too large (overlaps 0x10000)"
    head = (REPO / "boot/prebuilt/shim-head.bin").read_bytes()
    lk = (REPO / "boot/prebuilt/lk-modem.bin").read_bytes()

    shim = bytearray(IMAGE_OFFSET)
    shim[:len(head)] = head
    struct.pack_into("<QQ", shim, 16, IMAGE_OFFSET + effective_size, flags)
    shim[DTB_OFFSET:DTB_OFFSET + dtb_size] = dtb
    shim[LK_OFFSET:LK_OFFSET + len(lk)] = lk
    payload = bytes(shim) + image

    def tool(name, sub):
        for c in (a.aosp_tools / name, a.aosp_tools / sub / name):
            if c.exists():
                return c
        sys.exit(f"{name} not found in {a.aosp_tools} or {a.aosp_tools / sub}")
    mkbootimg, avbtool = tool("mkbootimg.py", "mkbootimg"), tool("avbtool.py", "avb")

    a.out.parent.mkdir(parents=True, exist_ok=True)
    gz = a.out.with_suffix(".payload.gz")
    gz.write_bytes(gzip.compress(payload, compresslevel=9, mtime=0))
    py = sys.executable
    subprocess.run([py, mkbootimg, "--header_version", "4",
                    "--kernel", gz, "--output", a.out], check=True)
    gz.unlink()
    subprocess.run([py, avbtool, "add_hash_footer", "--image", a.out,
                    "--partition_name", "boot", "--partition_size", str(PARTITION_SIZE),
                    "--key", a.key, "--algorithm", "SHA256_RSA2048",
                    "--prop", "com.android.build.boot.os_version:14",
                    "--prop", f"com.android.build.boot.fingerprint:{FINGERPRINT}",
                    "--prop", "com.android.build.boot.security_patch:2026-04-05"], check=True)
    if a.profile:
        profile = a.profile.read_bytes()
        assert profile[:2] == b"\x1f\x8b", "profile must be a .tar.gz"
        hdr = (b"CMF_BOOT_PROFILE_V1\n" + str(len(profile)).encode() + b"\n" +
               hashlib.sha256(profile).hexdigest().encode() + b"\n").ljust(PROFILE_HEADER, b"\0")
        with a.out.open("r+b") as f:
            f.seek(PROFILE_OFFSET)
            assert not any(f.read(PROFILE_HEADER + len(profile))), "profile area not empty"
            f.seek(PROFILE_OFFSET)
            f.write(hdr + profile)
    subprocess.run([py, avbtool, "verify_image", "--image", a.out,
                    "--key", a.key], check=True)
    print(f"{a.out}: sha256 {hashlib.sha256(a.out.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
