import mmap
import struct
import sys
from pathlib import Path

root = Path.home() / "mt6878-mainline"
sys.path.insert(0, str(root / "reference"))
from compare_fdt import read_fdt

stock = read_fdt(root / "reference/downstream-live.dtb", True)
pins = stock["nodes"]["/soc/apirq@11ce0000"]["mediatek,pins"]
expected = {n: (bank, bit, debounce) for n, bank, bit, debounce in struct.iter_unpack(">4I", pins)}

with Path(sys.argv[1]).open("rb") as kernel, mmap.mmap(kernel.fileno(), 0, access=mmap.ACCESS_READ) as elf:
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", elf)
    assert header[0][:6] == b"\x7fELF\x02\x01" and header[2] == 183
    shoff, shsize, shnum = header[6], header[11], header[12]
    sections = [struct.unpack_from("<IIQQQQIIQQ", elf, shoff + i * shsize) for i in range(shnum)]
    symtab, = [section for section in sections if section[1] == 2]
    strings = sections[symtab[6]]
    strbase = strings[4]
    results = []
    for pos in range(symtab[4], symtab[4] + symtab[5], symtab[9]):
        name, info, _, shndx, address, size = struct.unpack_from("<IBBHQQ", elf, pos)
        if not size or info & 15 != 1:
            continue
        strstart = strbase + name
        symbol = elf[strstart:elf.find(b"\0", strstart)]
        if symbol.split(b".", 1)[0] == b"eint_pins_mt6878":
            relative = address - sections[shndx][3]
            offset = sections[shndx][4] + relative
            results.append((symbol.decode(), elf[offset:offset + size]))
    (symbol, table), = results
    assert len(table) == 216 * 6, (symbol, len(table))
    for i, (n, bank, bit, debounce, flipped) in enumerate(struct.iter_unpack("<HBBBB", table)):
        if i in expected:
            assert n == i and (bank, bit, debounce) == expected[i] and not flipped, (i, n, bank, bit, debounce)
        else:
            assert bank == 255, (i, bank)
    print(f"{symbol}: {len(table)} bytes, 216 indexed slots, all 137 stock entries verified")
    print(f"EINT 212: bank {expected[212][0]}, bit {expected[212][1]}")
