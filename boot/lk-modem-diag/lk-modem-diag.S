/* SPDX-License-Identifier: GPL-2.0-only */
.section .text.entry, "ax"
.global _start
_start:
    b .Lentry
    .long 0
    .quad 0                       // Image text offset
    .quad 0                       // image_size, filled in by build-boot-image.py
    .quad 0                       // flags, filled in by build-boot-image.py
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0

.Lentry:
    // LK's x0 is its FDT. Keep it in x21 (callee-saved, so the C code keeps it).
    mov x21, x0
    // Dual boot: if an Android kernel is embedded (its payload offset is in
    // .Ldual_offset, filled in by build-boot-image.py), run the boot menu
    // (boot-menu.c). It returns 1 for Android: jump there with LK's own FDT.
    ldr x9, .Ldual_offset
    cbz x9, .Llinux
    mov x19, sp
    adr x20, _start
    add x9, x20, #0x1f0, lsl #12
    mov sp, x9
    bl cmf_boot_menu
    mov sp, x19
    cbz w0, .Llinux
    ldr x9, .Ldual_offset
    add x4, x20, x9
    mov x0, x21
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x4

.Llinux:
    // The helper reads LK's FDT (x0) and fills in ours; ours goes onward.
    mov x0, x21
    mov x19, sp
    adr x20, _start
    add x1, x20, #1, lsl #12
    add x9, x20, #0x1f0, lsl #12
    mov sp, x9
    bl cmf_lk_modem_diag
    dsb sy
    mov sp, x19
    add x0, x20, #1, lsl #12
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    add x4, x20, #0x200, lsl #12
    br x4

    .balign 8
    .ascii "CMFDUAL1"             // marker build-boot-image.py looks for
.Ldual_offset:
    .quad 0                       // Android Image offset in the payload; 0 = Linux only
