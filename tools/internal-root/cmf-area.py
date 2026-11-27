#!/usr/bin/env python3
"""Area table and stamps for the CMF internal Linux root.

The Linux root lives in a pinned, preallocated, immutable file in Android's F2FS
/data (created with `cmf-reserve create`). Its physical extents (FIEMAP, from
`cmf-reserve map`) are fixed. This tool turns that map into the table that
`cmf-dmarea` and /init use, and writes or checks the 4 KiB stamp at the start of
every 2 MiB segment. A stamp disappears if Android ever moves, deletes or resets
the file, and then Linux refuses the area and falls back to the microSD.

  cmf-area.py build  MAP PARTITION_SECTORS OUT_DIR   -> OUT_DIR/area.bin + area.json
  cmf-area.py stamp  AREA_BIN USERDATA_DEVICE        (writes stamps; device must be rw)
  cmf-area.py verify AREA_BIN USERDATA_DEVICE        (read only)

MAP is the output of `cmf-reserve map /data/cmf/linux.img` from Android.
PARTITION_SECTORS is `blockdev --getsz` of the userdata partition in Linux.
Run `stamp` only from Linux on the phone, with the partition set writable for
that step alone (blockdev --setrw / --setro).
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import secrets
import struct
import sys
import zlib
from pathlib import Path

SECTOR = 512
SEGMENT = 4096            # sectors (2 MiB)
STAMP = 8                 # sectors (4 KiB) at the start of each segment
STAMP_BYTES = STAMP * SECTOR


def parse_map(text, partition_sectors):
    header, *rows = text.splitlines()
    m = re.fullmatch(r"# st_size=(\d+) st_blocks=(\d+) logical_block_size=(\d+) inode=(\d+)", header)
    if not m:
        raise ValueError("invalid map header (expected cmf-reserve map output)")
    size, blocks, logical, inode = map(int, m.groups())
    if size % (SEGMENT * SECTOR) or blocks * SECTOR < size or logical != 4096 or inode == 0:
        raise ValueError("unexpected file size or block geometry")
    extents, segments, end, total = [], [], 0, 0
    for i, row in enumerate(rows):
        f = re.fullmatch(r"(\d+) (\d+) (\d+) (0x[\da-fA-F]+)", row)
        if not f:
            raise ValueError(f"malformed extent {i}")
        n, start, length = map(int, f.groups()[:3])
        flags = int(f[4], 16)
        if n != i or flags & ~0x1001 or bool(flags & 1) != (i == len(rows) - 1):
            raise ValueError(f"bad index or flags at extent {i}")
        if start % (SEGMENT * SECTOR) or length % (SEGMENT * SECTOR):
            raise ValueError(f"extent {i} is not 2 MiB aligned")
        start, length = start // SECTOR, length // SECTOR
        if start < end or length == 0 or start + length > partition_sectors:
            raise ValueError(f"extent {i} overlaps or falls outside the partition")
        end = start + length
        total += length
        extents.append({"start_sector": start, "length_sectors": length, "flags": flags})
        segments.extend(range(start, end, SEGMENT))
    if total * SECTOR != size:
        raise ValueError("extent total differs from the file size")
    return size, extents, segments


def encode(area_id, digest, partition_sectors, segments):
    body = struct.pack("<8s16s32sQI", b"CMFTBL01", area_id, digest, partition_sectors, len(segments))
    body += struct.pack(f"<{len(segments)}Q", *segments)
    return body + struct.pack("<I", zlib.crc32(body))


def decode(raw):
    if raw[:8] != b"CMFTBL01" or zlib.crc32(raw[:-4]) != struct.unpack("<I", raw[-4:])[0]:
        raise ValueError("invalid area.bin")
    area_id, digest = raw[8:24], raw[24:56]
    partition_sectors, count = struct.unpack_from("<QI", raw, 56)
    segments = struct.unpack_from(f"<{count}Q", raw, 68)
    return area_id, digest, partition_sectors, segments


def stamp_block(area_id, digest, index, start):
    s = struct.pack("<8s16sIQQ32s", b"CMFAREA1", area_id, index, start, SEGMENT, digest)
    return (s + struct.pack("<I", zlib.crc32(s))).ljust(STAMP_BYTES, b"\0")


def cmd_build(a):
    text = a.map.read_text("ascii")
    size, extents, segments = parse_map(text, a.partition_sectors)
    digest = hashlib.sha256(text.encode()).digest()
    area_id = secrets.token_bytes(16)
    a.out_dir.mkdir(mode=0o700)
    (a.out_dir / "area.bin").write_bytes(encode(area_id, digest, a.partition_sectors, segments))
    (a.out_dir / "area.json").write_text(json.dumps({
        "area_id": area_id.hex(), "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "map_sha256": digest.hex(), "partition_sectors": a.partition_sectors, "file_bytes": size,
        "segment_sectors": SEGMENT, "stamp_sectors": STAMP, "extents": extents,
        "segment_start_sectors": segments}, indent=2) + "\n")
    data = len(segments) * (SEGMENT - STAMP) * SECTOR
    print(f"{len(extents)} extents, {len(segments)} segments, {data} bytes for the Linux root")


def check(fd, area_id, digest, segments):
    bad = []
    for i, start in enumerate(segments):
        if os.pread(fd, STAMP_BYTES, start * SECTOR) != stamp_block(area_id, digest, i, start):
            bad.append(i)
    return bad


def cmd_stamp(a):
    area_id, digest, psec, segments = decode(a.area_bin.read_bytes())
    fd = os.open(a.device, os.O_RDWR | os.O_SYNC)
    if os.lseek(fd, 0, os.SEEK_END) // SECTOR != psec:
        sys.exit("device size differs from the table; wrong partition?")
    for i, start in enumerate(segments):
        os.pwrite(fd, stamp_block(area_id, digest, i, start), start * SECTOR)
    os.fsync(fd)
    bad = check(fd, area_id, digest, segments)
    os.close(fd)
    print(f"stamps written={len(segments)} verified_bad={len(bad)}")
    sys.exit(1 if bad else 0)


def cmd_verify(a):
    area_id, digest, psec, segments = decode(a.area_bin.read_bytes())
    fd = os.open(a.device, os.O_RDONLY)
    bad = check(fd, area_id, digest, segments)
    os.close(fd)
    print(f"stamps ok={len(segments) - len(bad)} bad={len(bad)}" + (f" first_bad={bad[0]}" if bad else ""))
    sys.exit(1 if bad else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("map", type=Path)
    b.add_argument("partition_sectors", type=int)
    b.add_argument("out_dir", type=Path)
    for name in ("stamp", "verify"):
        p = sub.add_parser(name)
        p.add_argument("area_bin", type=Path)
        p.add_argument("device")
    a = ap.parse_args()
    try:
        {"build": cmd_build, "stamp": cmd_stamp, "verify": cmd_verify}[a.cmd](a)
    except (OSError, ValueError) as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
