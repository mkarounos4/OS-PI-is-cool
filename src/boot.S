// =============================================================
//  boot.S  —  AArch64 bare-metal entry for Raspberry Pi 5
// =============================================================
.section .text.boot
.global _start
_start:
    mrs  x0, mpidr_el1
    and  x0, x0, #0xFF
    cbnz x0, _hang

    ldr  x0, =_stack_top
    mov  sp, x0

    mov  x29, xzr
    mov  x30, xzr

    ldr  x0, =__bss_start
    ldr  x1, =__bss_end
1:
    cmp  x0, x1
    b.hs 2f
    str  xzr, [x0], #8
    b    1b

2:
    bl kernel_main

_hang:
    wfe
    b _hang
