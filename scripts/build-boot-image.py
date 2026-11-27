#!/usr/bin/env python3
"""Assemble and sign a CMF Phone 1 boot image (boot_a) from a kernel Image and DTB.

usage: build-boot-image.py --image Image --dtb cmf-phone-1.dtb --key avb.pem \
                           --aosp-tools DIR --out boot.img [--profile profile.tar.gz]
                           [--android-boot android-boot.img] [--payload-out payload.bin]

Layout of the kernel payload (gzip-compressed, as LK expects):
  0x000000  shim header (boot/prebuilt/shim-head.bin): an arm64 Image header whose
            code passes the embedded DTB to the kernel and jumps to +2 MiB
  0x001000  DTB (must end before 0x10000)
  0x010000  LK modem-record helper (boot/prebuilt/lk-modem.bin, source in
            boot/lk-modem-diag/) that forwards LK's modem record to the kernel DTB
  0x200000  the kernel Image (with its built-in initramfs)
  (dual)    with --android-boot: the Android kernel from that boot image, at the
            next 2 MiB boundary after Linux. Holding Volume Down at power-on
            boots it with LK's own device tree and ramdisks (Android needs its
            matching init_boot/vendor_boot in the same slot); otherwise Linux.

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
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DTB_OFFSET, LK_OFFSET, IMAGE_OFFSET = 0x1000, 0x10000, 0x200000
ALIGN = 0x200000
KERNEL_AREA = 0x7c80000   # LK's mb_kernel (0x40000000..0x47c80000, then its DTB)
PARTITION_SIZE = 64 * 1024 * 1024
PROFILE_OFFSET, PROFILE_HEADER = 32 * 1024 * 1024, 4096
# AVB properties copied from the stock CMF Phone 1 boot image (Android 14 build).
FINGERPRINT = ("alps/hal_mgvi_64_64only_ww_armv82/mgvi_64_64only_ww_armv82:"
               "14/UP1A.231005.007/2609281140:user/release-keys")


def android_kernel(path):
    """The (decompressed) arm64 Image from an Android boot image."""
    d = path.read_bytes()
    assert d[:8] == b"ANDROID!", f"{path} is not an Android boot image"
    size, version = struct.unpack_from("<I", d, 8)[0], struct.unpack_from("<I", d, 40)[0]
    page = 4096 if version >= 3 else struct.unpack_from("<I", d, 36)[0]
    k = d[page:page + size]
    if k[:4] == b"\x02\x21\x4c\x18":          # LZ4 legacy, as Android uses
        k = subprocess.run(["lz4", "-dc"], input=k, capture_output=True, check=True).stdout
    elif k[:2] == b"\x1f\x8b":
        k = gzip.decompress(k)
    assert k[56:60] == b"ARMd", "Android kernel is not an arm64 Image"
    assert struct.unpack_from("<Q", k, 8)[0] == 0, "Android kernel text_offset is not 0"
    return k


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True, type=Path)
    ap.add_argument("--dtb", required=True, type=Path)
    ap.add_argument("--key", type=Path)
    ap.add_argument("--aosp-tools", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--profile", type=Path)
    ap.add_argument("--android-boot", type=Path,
                    help="Android boot image whose kernel to embed (Volume Down at power-on)")
    ap.add_argument("--payload-out", type=Path,
                    help="also write the uncompressed kernel payload (for QEMU tests)")
    a = ap.parse_args()
    if a.out and not (a.key and a.aosp_tools):
        ap.error("--out needs --key and --aosp-tools")
    if not (a.out or a.payload_out):
        ap.error("give --out and/or --payload-out")

    image = a.image.read_bytes()
    _, effective_size, flags = struct.unpack_from("<QQQ", image, 8)
    assert image[56:60] == b"ARMd", "not an arm64 Image"
    dtb = a.dtb.read_bytes()
    magic, dtb_size = struct.unpack_from(">II", dtb)
    assert magic == 0xD00DFEED and dtb_size == len(dtb), "bad DTB"
    assert DTB_OFFSET + dtb_size <= LK_OFFSET, "DTB too large (overlaps 0x10000)"
    head = (REPO / "boot/prebuilt/shim-head.bin").read_bytes()
    lk = (REPO / "boot/prebuilt/lk-modem.bin").read_bytes()

    assert len(head) <= DTB_OFFSET and len(lk) <= IMAGE_OFFSET - LK_OFFSET
    shim = bytearray(IMAGE_OFFSET)
    shim[:len(head)] = head
    total = IMAGE_OFFSET + effective_size
    tail = b""
    if a.android_boot:
        android = android_kernel(a.android_boot)
        marker = head.find(b"CMFDUAL1")
        assert marker >= 0, "shim-head.bin has no dual-boot support (rebuild boot/lk-modem-diag)"
        offset = -(-total // ALIGN) * ALIGN
        struct.pack_into("<Q", shim, marker + 8, offset)
        tail = bytes(offset - IMAGE_OFFSET - len(image)) + android
        total = offset + struct.unpack_from("<Q", android, 16)[0]
        print(f"android kernel at payload +{offset:#x}, {len(android)} bytes")
    assert total <= KERNEL_AREA, f"payload needs {total:#x}, LK's kernel area is {KERNEL_AREA:#x}"
    struct.pack_into("<QQ", shim, 16, total, flags)
    shim[DTB_OFFSET:DTB_OFFSET + dtb_size] = dtb
    shim[LK_OFFSET:LK_OFFSET + len(lk)] = lk
    payload = bytes(shim) + image + tail
    if a.payload_out:
        a.payload_out.write_bytes(payload)
    if not a.out:
        return

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
    # avbtool verify_image looks for "<partition>.img" next to the image, so
    # verify a copy named boot.img in a temporary directory.
    with tempfile.TemporaryDirectory() as tmp:
        copy = Path(tmp) / "boot.img"
        shutil.copyfile(a.out, copy)
        subprocess.run([py, avbtool, "verify_image", "--image", copy,
                        "--key", a.key], check=True)
    print(f"{a.out}: sha256 {hashlib.sha256(a.out.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
