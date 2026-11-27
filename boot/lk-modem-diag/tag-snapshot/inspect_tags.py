"""Check MediaTek format-3 modem tag metadata without reading shared memory."""
import struct

TAG_HEADER = struct.Struct("<64sIII")
SMEM_REGION = struct.Struct("<QQIIIIII")
MODEM_BLOCK = struct.Struct("<IIIIQ")


def read_tags(snapshot, count):
    tags = {}
    offset = 0
    for index in range(count):
        name_bytes, start, size, following = TAG_HEADER.unpack_from(snapshot, offset)
        name, terminator, _ = name_bytes.partition(bytes(1))
        if not terminator or not name or start + size > len(snapshot):
            raise ValueError(f"Invalid tag header at index {index}")
        name = name.decode("ascii")
        if name in tags:
            raise ValueError(f"Duplicate tag {name}")
        tags[name] = snapshot[start:start + size]
        if index + 1 < count and following < offset + TAG_HEADER.size:
            raise ValueError(f"Invalid next offset for tag {name}")
        offset = following
    return tags


def fits(window, base, size):
    start, length = window
    return start <= base and base + size <= start + length


def check_shared(tags, key, window):
    count, = struct.unpack("<I", tags[f"{key}_num"])
    table = tags[key]
    if count == 0 or len(table) != count * SMEM_REGION.size:
        raise ValueError(f"Invalid table size for {key}")
    entries = list(SMEM_REGION.iter_unpack(table))
    invalid = [
        (region, f"{base:#x}", f"{size:#x}")
        for base, _, region, _, size, _, _, _ in entries
        if size and not fits(window, base, size)
    ]
    return count, invalid


def inspect(snapshot, tag_count, reservations):
    tags = read_tags(snapshot, tag_count)
    lines = [f"Decoded {len(tags)} LK tag headers."]
    all_valid = True
    for kind, key in (("noncached", "nc_smem_layout"), ("cached", "c_smem_layout")):
        count, invalid = check_shared(tags, key, reservations[kind][0])
        lines.append(f"{kind} shared memory: {count} entries, {len(invalid)} outside its reserved range.")
        for region, base, size in invalid:
            lines.append(f"  Shared-memory ID {region}, range {base} + {size}")
        all_valid &= not invalid

    table = tags["md_mem_layout"]
    blocks = list(MODEM_BLOCK.iter_unpack(table))
    if not blocks:
        raise ValueError("Empty modem image memory table")
    missing = [
        (f"{base:#x}", f"{size:#x}")
        for _, size, _, _, base in blocks
        if size and not any(fits(window, base, size) for window in reservations["modem"])
    ]
    lines.append(f"Modem image memory: {len(blocks)} entries, {len(missing)} outside the reserved ranges.")
    for base, size in missing:
        lines.append(f"  Modem image range {base} + {size}")
    lines.append(f"Shared and modem memory ranges validated: {bool(all_valid and not missing)}")
    return "\n".join(lines) + "\n"
