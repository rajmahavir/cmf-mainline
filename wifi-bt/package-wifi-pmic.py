#!/usr/bin/env python3
"""Package a rebuilt CMF Image and DTB using the verified diagnostic v9 shim."""
import argparse
import gzip
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

V9_SHA256 = "c05205440c7baef1fa68a89b6cabf5b422247fca10e95121f3678c9d67d98cfe"
DTB_OFFSET = 0x1000
IMAGE_OFFSET = 0x200000
PARTITION_SIZE = 67108864
PROFILE_OFFSET = 32 * 1024 * 1024
PROFILE_HEADER_SIZE = 4096
PROFILE_LIMIT = 12 * 1024 * 1024
PROFILE_MARKER = b"CMF_BOOT_PROFILE_V1"
FINGERPRINT = (
    "alps/hal_mgvi_64_64only_ww_armv82/mgvi_64_64only_ww_armv82:"
    "14/UP1A.231005.007/2609281140:user/release-keys"
)


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def append_profile(boot, profile_path):
    profile = profile_path.read_bytes()
    if not profile.startswith(b"\x1f\x8b") or len(profile) > PROFILE_LIMIT:
        raise ValueError("Expected a gzip profile no larger than 12 MiB")
    if boot.stat().st_size > PROFILE_OFFSET:
        raise ValueError("Boot image overlaps the private profile offset")
    digest = hashlib.sha256(profile).hexdigest().encode("ascii")
    header = b"\n".join((PROFILE_MARKER, str(len(profile)).encode("ascii"), digest, b""))
    with boot.open("r+b") as image:
        image.seek(PROFILE_OFFSET)
        image.write(header.ljust(PROFILE_HEADER_SIZE, b"\0"))
        image.write(profile)
    return header, profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "image", "dtb", "packaging", "output"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--avb-key", type=Path, help="AVB key or public key for delegated signing")
    parser.add_argument("--signing-helper", type=Path, help="AVB signing helper executable")
    parser.add_argument("--private-profile", type=Path, help="Local gzip boot profile to append before signing")
    args = parser.parse_args()
    boot_v9 = args.base.read_bytes()
    if hashlib.sha256(boot_v9).hexdigest() != V9_SHA256:
        raise ValueError("Base must be the verified diagnostic v9 boot image")
    if boot_v9[:8] != b"ANDROID!" or struct.unpack_from("<I", boot_v9, 40)[0] != 4:
        raise ValueError("Expected Android boot header v4")
    kernel_size = struct.unpack_from("<I", boot_v9, 8)[0]
    v9_payload = gzip.decompress(boot_v9[0x1000:0x1000 + kernel_size])
    if v9_payload[56:60] != b"ARMd" or v9_payload[IMAGE_OFFSET + 56:IMAGE_OFFSET + 60] != b"ARMd":
        raise ValueError("Invalid v9 arm64 image headers")
    image = args.image.read_bytes()
    text_offset, effective_size, flags = struct.unpack_from("<QQQ", image, 8)
    if image[56:60] != b"ARMd" or text_offset or len(image) > effective_size:
        raise ValueError("Invalid arm64 kernel Image")
    dtb = args.dtb.read_bytes()
    magic, dtb_size = struct.unpack_from(">II", dtb)
    if magic != 0xD00DFEED or dtb_size != len(dtb) or DTB_OFFSET + dtb_size > IMAGE_OFFSET:
        raise ValueError("Invalid or oversized flattened DT")
    shim = bytearray(v9_payload[:IMAGE_OFFSET])
    shim[DTB_OFFSET:] = b"\0" * (IMAGE_OFFSET - DTB_OFFSET)
    shim[DTB_OFFSET:DTB_OFFSET + dtb_size] = dtb
    struct.pack_into("<QQ", shim, 16, IMAGE_OFFSET + effective_size, flags)
    payload = shim + image
    args.output.mkdir()
    output = args.output.resolve()
    shutil.copyfile(args.dtb, output / "candidate.dtb")
    (output / "payload.bin.gz").write_bytes(gzip.compress(payload, compresslevel=9, mtime=0))
    boot = output / "boot.img"
    tools = args.packaging / "tools"
    run(sys.executable, tools / "mkbootimg/mkbootimg.py", "--header_version", "4",
        "--kernel", output / "payload.bin.gz", "--output", boot)
    if args.private_profile:
        header, profile = append_profile(boot, args.private_profile)
    avb = tools / "avb/avbtool.py"
    key = args.avb_key or args.packaging / "keys/candidate-avb.pem"
    signing = ["--signing_helper", args.signing_helper] if args.signing_helper else []
    run(sys.executable, avb, "add_hash_footer", "--image", boot,
        "--partition_name", "boot", "--partition_size", str(PARTITION_SIZE),
        "--key", key, "--algorithm", "SHA256_RSA2048",
        "--salt", "b16a0ade6b083b84412dbb52857e7116386b9ca2633d8fd4fab1a7d6372b2e62",
        "--flags", "0", "--prop", "com.android.build.boot.os_version:14",
        "--prop", f"com.android.build.boot.fingerprint:{FINGERPRINT}",
        "--prop", "com.android.build.boot.security_patch:2026-04-05", *signing)
    with (output / "avb-verification.txt").open("w") as log:
        run(sys.executable, avb, "verify_image", "--image", boot, "--key", key, stdout=log)
    signed = boot.read_bytes()
    signed_kernel_size = struct.unpack_from("<I", signed, 8)[0]
    if len(signed) != PARTITION_SIZE or gzip.decompress(signed[0x1000:0x1000 + signed_kernel_size]) != payload:
        raise ValueError("Signed boot image differs from the verified payload")
    if args.private_profile:
        data_start = PROFILE_OFFSET + PROFILE_HEADER_SIZE
        if (not signed[PROFILE_OFFSET:data_start].startswith(header)
                or signed[data_start:data_start + len(profile)] != profile):
            raise ValueError("Signed boot image differs from the private profile")
    manifest = {}
    for path in (boot, output / "candidate.dtb", args.image):
        manifest[path.name] = {
            "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
