.section .text.user_entry, "ax"
.global _start

_start:
    // x0 = argc
    // x1 = argv
    stp x0, x1, [sp, #-16]!

    movz x0, #0x40, lsl #16
    movz x1, #0x4000
    movk x1, #0x40, lsl #16
    bl mem_init

    ldp x0, x1, [sp], #16

    bl main

    // calls exit with main's return value by default (as x0)
    bl exit

1:
    b 1b
