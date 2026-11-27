.section .text
.global _start
_start:
    b _stub_entry
    .long 0
    .quad 0
    .quad 40239104
    .quad 10
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0
_stub_entry:
    adr x0, .Ldtb
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    adr x4, .
    sub x4, x4, #0x50
    add x4, x4, #0x200, lsl #12
    br x4
.balign 4096
.Ldtb:
    .incbin "candidate.dtb"
.org 0x200000
    .incbin "Image"
