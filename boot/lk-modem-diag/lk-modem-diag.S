/* SPDX-License-Identifier: GPL-2.0-only */
.section .text.entry, "ax"
.global _start
_start:
    b .Lentry
    .long 0
    .quad 0                       // Image text offset
    .quad 0                       // Filled in by package-wifi-pmic.py
    .quad 0                       // Filled in by package-wifi-pmic.py
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0

.Lentry:
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
