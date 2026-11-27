/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CMF_KEY_ADDR
#define CMF_KEY_ADDR 0x10005210   /* GPIO DIN bank 1; QEMU tests override it */
#endif

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
    // Dual boot: if an Android kernel is embedded (its payload offset is in
    // .Ldual_offset, filled in by build-boot-image.py) and Volume Down (GPIO
    // 54, active low, bit 22 of GPIO DIN bank 1) reads "held" on all 8
    // samples, jump to the Android kernel with LK's own x0 (FDT) untouched.
    // Any "released" sample boots Linux. Only x9-x14 are used here.
    ldr x9, .Ldual_offset
    cbz x9, .Llinux
    ldr x10, =CMF_KEY_ADDR
    mov w13, #8
1:  ldr w11, [x10]
    tbnz w11, #22, .Llinux
    mov x14, #0x4000
2:  subs x14, x14, #1
    b.ne 2b
    subs w13, w13, #1
    b.ne 1b
    adr x10, _start
    add x4, x10, x9
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x4

.Llinux:
    // LK's x0 is the source FDT. Only the embedded FDT is passed onward.
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

    .ltorg
    .balign 8
    .ascii "CMFDUAL1"             // marker build-boot-image.py looks for
.Ldual_offset:
    .quad 0                       // Android Image offset in the payload; 0 = Linux only
