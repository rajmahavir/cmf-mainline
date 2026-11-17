.global _start
.text
_start:
    movz w0, #0xfee1, lsl #16
    movk w0, #0xdead
    movz w1, #0x1969
    movk w1, #0x2812, lsl #16
    movz w2, #0xc3d4
    movk w2, #0xa1b2, lsl #16
    adr x3, mode
    mov x8, #142
    svc #0
    mov x0, #1
    mov x8, #93
    svc #0
mode: .asciz "bootloader"
