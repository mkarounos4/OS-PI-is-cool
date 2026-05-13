.section ".text.boot"

.global _start

_start:

    // Set stack pointer
    ldr x0, =_stack_top
    mov sp, x0

    // Clear BSS
    ldr x1, =__bss_start
    ldr x2, =__bss_end

clear_bss:
    cmp x1, x2
    b.ge bss_done

    str xzr, [x1], #8
    b clear_bss

bss_done:

    // Jump to C kernel
    bl kernel_main

hang:
    b hang
