# Simple Framebuffer & Console Font Bring-up

## Summary
The Nothing CMF Phone 1 (A015 / MT6878 / Tetris) features a 1080×2400 AMOLED display driven by MediaTek's RDMA display pipeline. In mainline Linux 7.2, early display output is achieved via `simple-framebuffer` under `/chosen`, which hands off the pre-initialized Little Kernel (LK) framebuffer directly to `simpledrm` and `fbcon`.

## Hardware & Framebuffer Parameters
- **Resolution:** 1080 × 2400
- **Color Format:** 32bpp (`a8r8g8b8` / `XR24`)
- **Physical Address Range:** `0xfe0d0000` – `0x1fffffff` (`0x1f2f000` bytes)
- **Calculated Line Width:** $1080 \times 4 = 4320$ bytes
- **Hardware Pitch (Stride):** $4352$ bytes (`0x1100`)

### RDMA Pitch Alignment & Diagonal Shearing
When configured with the nominal stride of 4320 bytes, the hardware display controller suffered scanline drift of 32 bytes (8 pixels) per scanline, creating severe diagonal bars and horizontal text shearing. 

MediaTek RDMA scanout controllers require pitch to be aligned to **64-byte boundaries**:
$$\frac{4320}{64} = 67.5 \implies \lceil 67.5 \rceil \times 64 = 68 \times 64 = 4352\text{ bytes}$$

Setting `stride = <4352>` in the DT node completely resolved all shearing, yielding a straight, distortion-free display.

## Device Tree Configuration

### `/chosen` node
```dts
framebuffer@fe0d0000 {
    compatible = "simple-framebuffer";
    reg = <0x00 0xfe0d0000 0x00 0x1f2f000>;
    width = <1080>;
    height = <2400>;
    stride = <4352>;
    format = "a8r8g8b8";
};
```

### Reserved Memory (`mblock-15-framebuffer`)
The memory reservation in `reserved-memory` must include `no-map;`:
```dts
mblock-15-framebuffer {
    reg = <0x00 0xfe0d0000 0x00 0x1f2f000>;
    no-map;
};
```
Without `no-map;`, the kernel maps the region as standard cached RAM early in boot, causing `simpledrm`'s `ioremap_wc()` to fail with `-ENOMEM` / `-EBUSY` ("ioremap attempted on RAM pfn").

## HiDPI Console Font Scaling ($16\times 32$)
The default kernel console font is $8\times 16$ (`CONFIG_FONT_8x16=y`), resulting in a tiny, unreadable $135\times 150$ character grid on a 1080×2400 mobile screen.

1. **Font:** Terminus Bold 16×32 bitmap font (256 characters × 64 bytes/char = 16,384 bytes raw glyph bitmap).
2. **Dynamic Injection:** Built `tools/setfont32.c`, a freestanding static aarch64 binary issuing the `KDFONTOP` (`0x4B72`) ioctl with `KD_FONT_OP_SET` to `/dev/tty0`.
3. **Console Grid:** Doubled the character dimensions, shifting the grid to **67 columns × 75 rows**.
