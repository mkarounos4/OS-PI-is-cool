.section .text.scheduler, "ax"
.align 2

.equ CPU_CTX_X19,       0
.equ CPU_CTX_X21,       16
.equ CPU_CTX_X23,       32
.equ CPU_CTX_X25,       48
.equ CPU_CTX_X27,       64
.equ CPU_CTX_X29,       80
.equ CPU_CTX_SP,        96
.equ CPU_CTX_TTBR0_EL1, 104

.global context_switch
context_switch:
    stp x19, x20, [x0, #CPU_CTX_X19]
    stp x21, x22, [x0, #CPU_CTX_X21]
    stp x23, x24, [x0, #CPU_CTX_X23]
    stp x25, x26, [x0, #CPU_CTX_X25]
    stp x27, x28, [x0, #CPU_CTX_X27]
    stp x29, x30, [x0, #CPU_CTX_X29]
    mov x2, sp
    str x2, [x0, #CPU_CTX_SP]
    mrs x2, ttbr0_el1
    str x2, [x0, #CPU_CTX_TTBR0_EL1]

    ldp x19, x20, [x1, #CPU_CTX_X19]
    ldp x21, x22, [x1, #CPU_CTX_X21]
    ldp x23, x24, [x1, #CPU_CTX_X23]
    ldp x25, x26, [x1, #CPU_CTX_X25]
    ldp x27, x28, [x1, #CPU_CTX_X27]
    ldp x29, x30, [x1, #CPU_CTX_X29]
    ldr x2, [x1, #CPU_CTX_TTBR0_EL1]
    dsb ishst
    msr ttbr0_el1, x2
    tlbi vmalle1is
    dsb ish
    isb
    ldr x2, [x1, #CPU_CTX_SP]
    mov sp, x2
    ret

.global context_switch_to
context_switch_to:
    ldp x19, x20, [x0, #CPU_CTX_X19]
    ldp x21, x22, [x0, #CPU_CTX_X21]
    ldp x23, x24, [x0, #CPU_CTX_X23]
    ldp x25, x26, [x0, #CPU_CTX_X25]
    ldp x27, x28, [x0, #CPU_CTX_X27]
    ldp x29, x30, [x0, #CPU_CTX_X29]
    ldr x2, [x0, #CPU_CTX_TTBR0_EL1]
    dsb ishst
    msr ttbr0_el1, x2
    tlbi vmalle1is
    dsb ish
    isb
    ldr x2, [x0, #CPU_CTX_SP]
    mov sp, x2
    ret
