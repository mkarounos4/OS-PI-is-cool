.section .text.user_entry, "ax"
.global _start

_start:
    // x0 = argc
    // x1 = argv

    bl main

    // calls exit with main's return value by default (as x0)
    bl exit

1:
    b 1b
