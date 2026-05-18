// =============================================================
//  boot.S  —  AArch64 bare-metal entry for Raspberry Pi 5
// =============================================================
.section .text.boot
.global _start
_start:
    // Idle other 3 cores
    mrs  x0, mpidr_el1
    and  x0, x0, #0xFF
    cbnz x0, _hang

    // Load stack pointer
    ldr  x0, =_stack_top
    mov  sp, x0

    // Initialize other stack registers to 0
    mov  x29, xzr
    mov  x30, xzr

    // Set up current exception level
    mrs  x1, CurrentEL
    lsr  x1, x1, #2
    cmp  x1, #1
    b.eq _clear_bss

    cmp  x1, #2
    b.ne _hang

    msr  sp_el1, x0

    mov  x2, #(1 << 31)
    msr  hcr_el2, x2

    mov  x2, #3
    msr  cnthctl_el2, x2
    msr  cntvoff_el2, xzr

    ldr  x2, =_enter_el1
    msr  elr_el2, x2

    mov  x2, #0x3c5
    msr  spsr_el2, x2
    eret

_enter_el1:
    ldr  x0, =_stack_top
    mov  sp, x0
    mov  x29, xzr
    mov  x30, xzr

_clear_bss:
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
