/* SPDX-License-Identifier: GPL-2.0-only */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .entry : { KEEP(*(.text.entry)) }
    ASSERT(SIZEOF(.entry) <= 0x1000, "entry overlaps device tree")
    . = 0x10000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "shim requires a zero-initialized section")
    ASSERT(. <= 0x20000, "diagnostic exceeds allowed size")
    /DISCARD/ : { *(.comment .note.* .eh_frame*) }
}
