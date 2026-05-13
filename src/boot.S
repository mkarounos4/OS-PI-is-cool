// =============================================================
//  boot.S  —  AArch64 bare-metal entry for Raspberry Pi 5
// =============================================================
.section ".text.boot"
.global _start

_start:
    // -------------------------------------------------------
    // Park cores 1-3.  The Pi 5 firmware releases only core 0
    // to this address; the others spin in the firmware spin-
    // table.  This guard is an extra safety net.
    // -------------------------------------------------------
    mrs  x0, mpidr_el1
    and  x0, x0, #0xFF          // isolate Aff0 (core ID 0-3)
    cbnz x0, _hang              // non-zero  →  not core 0, hang

    // -------------------------------------------------------
    // Set up the stack pointer (grows downward from _stack_top)
    // -------------------------------------------------------
    ldr  x0, =_stack_top
    mov  sp, x0

    // -------------------------------------------------------
    // Zero the BSS section
    // -------------------------------------------------------
    ldr  x1, =__bss_start
    ldr  x2, =__bss_end
_clear_bss:
    cmp  x1, x2
    b.ge _bss_done
    str  xzr, [x1], #8
    b    _clear_bss
_bss_done:

    // -------------------------------------------------------
    // Jump into C  (should never return; hang if it does)
    // -------------------------------------------------------
    bl   kernel_main

_hang:
    wfe                         // sleep until an event, saves power
    b    _hang
