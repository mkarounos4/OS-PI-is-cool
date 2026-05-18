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

    bl early_rp1_gpio4_on
    bl early_rp1_uart0_marker

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

early_rp1_gpio4_on:
    ldr  x0, =0x1c000f0014     // PADS_BANK0 GPIO4
    mov  w1, #0x62             // IE | 8mA | schmitt, OD=0
    str  w1, [x0]

    ldr  x0, =0x1c000d0024     // IO_BANK0 GPIO4_CTRL
    mov  w1, #5                // SYS_RIO
    str  w1, [x0]

    ldr  x0, =0x1c000e2004     // RIO0 OE_SET
    mov  w1, #0x10
    str  w1, [x0]

    ldr  x0, =0x1c000e2000     // RIO0 OUT_SET
    str  w1, [x0]
    dsb  sy
    ret

early_rp1_uart0_marker:
    ldr  x0, =0x1c00030000     // UART0 DR, pre-initialized by enable_rp1_uart=1
    mov  w1, #'B'
    str  w1, [x0]
    mov  w1, #'\r'
    str  w1, [x0]
    mov  w1, #'\n'
    str  w1, [x0]
    dsb  sy
    ret
