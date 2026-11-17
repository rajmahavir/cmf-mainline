#!/usr/bin/env python3
"""Insert a private RAM bundle into a v16 template without changing its layout."""
import argparse
import os
from pathlib import Path

SLOT_SIZE = 12 * 1024 * 1024
MARKER = b"CMF-V16-PRIVATE-BUNDLE-PLACEHOLDER\n"
HEADER_SIZE = 11

parser = argparse.ArgumentParser(description=__doc__)
for name in ("template", "bundle", "output"):
    parser.add_argument(name, type=Path)
args = parser.parse_args()

image = args.template.read_bytes()
bundle = args.bundle.read_bytes()
if not bundle.startswith(b"\x1f\x8b") or len(bundle) > SLOT_SIZE - HEADER_SIZE:
    parser.error("expected a gzip archive that fits in the private bundle slot")

start = image.find(MARKER)
end = start + SLOT_SIZE
if (
    start < 0
    or image.count(MARKER) != 1
    or image[start:end] != MARKER.ljust(SLOT_SIZE, b"\0")
):
    parser.error("the template must contain exactly one intact private bundle slot")

header = f"{len(bundle):010d}\n".encode("ascii")
slot = (header + bundle).ljust(SLOT_SIZE, b"\0")
os.umask(0o077)
with args.output.open("xb") as dest:
    dest.write(image[:start])
    dest.write(slot)
    dest.write(image[end:])
