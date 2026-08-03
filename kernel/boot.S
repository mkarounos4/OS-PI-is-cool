// =============================================================
//  boot.S  —  AArch64 bare-metal entry for Raspberry Pi 5
// =============================================================
.section .text.boot.entry, "ax"
.global _start
_start:
    mrs  x0, mpidr_el1
    and  x0, x0, #0xFF
    cbnz x0, _secondary_wait_for_boot_tables

    // Load stack pointer
    ldr  x0, =_stack_top_phys
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
    ldr  x0, =_stack_top_phys
    mov  sp, x0
    mov  x29, xzr
    mov  x30, xzr

_clear_bss:
    ldr  x0, =__bss_start_phys
    ldr  x1, =__bss_end_phys
1:
    cmp  x0, x1
    b.hs 2f
    str  xzr, [x0], #8
    b    1b

2:
    bl initialize_vm

_secondary_wait_for_boot_tables:
    cmp x0, #4
    b.hs _hang
    mov x4, x0
    ldr x1, =secondary_boot_table_ready
1:
    wfe
    ldr x2, [x1]
    cbz x2, 1b
    mov x0, x4
    b _secondary_start

.global _secondary_start
_secondary_start:
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cmp x0, #4
    b.hs _hang

    ldr x1, =secondary_boot_stacks
    add x1, x1, x0, lsl #12
    add x1, x1, #4096
    mov sp, x1
    mov x29, xzr
    mov x30, xzr

    mrs x2, CurrentEL
    lsr x2, x2, #2
    cmp x2, #1
    b.eq 2f

    cmp x2, #2
    b.ne _hang

    msr sp_el1, x1

    mov x3, #(1 << 31)
    msr hcr_el2, x3

    mov x3, #3
    msr cnthctl_el2, x3
    msr cntvoff_el2, xzr

    ldr x3, =1f
    msr elr_el2, x3

    mov x3, #0x3c5
    msr spsr_el2, x3
    eret

1:
    mov sp, x1
    mov x29, xzr
    mov x30, xzr

2:
    bl initialize_secondary_vm

_hang:
    wfe
    b _hang

.section .bss.boot_stacks, "aw", %nobits
.balign 16
.global secondary_boot_table_ready
secondary_boot_table_ready:
    .skip 8
.balign 4096
secondary_boot_stacks:
    .skip 4096 * 4

.section .rodata.secondary_boot, "a"
.balign 8
.global secondary_start_phys
secondary_start_phys:
    .quad _secondary_start
