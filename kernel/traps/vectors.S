.equ TRAP_FRAME_SIZE, 304

.macro VECTOR_ENTRY type
    sub sp, sp, #TRAP_FRAME_SIZE
    stp x0, x1, [sp, #0]
    mov x0, #\type
    b exception_entry
    .align 7
.endm

.section .text.traps, "ax"
.align 11
.global exception_vectors
exception_vectors:
    VECTOR_ENTRY 0
    VECTOR_ENTRY 1
    VECTOR_ENTRY 2
    VECTOR_ENTRY 3

    VECTOR_ENTRY 4
    VECTOR_ENTRY 5
    VECTOR_ENTRY 6
    VECTOR_ENTRY 7

    VECTOR_ENTRY 8
    VECTOR_ENTRY 9
    VECTOR_ENTRY 10
    VECTOR_ENTRY 11

    VECTOR_ENTRY 12
    VECTOR_ENTRY 13
    VECTOR_ENTRY 14
    VECTOR_ENTRY 15

.global exception_entry
exception_entry:
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    add x1, sp, #TRAP_FRAME_SIZE
    str x1, [sp, #248]

    mrs x1, elr_el1
    str x1, [sp, #256]
    mrs x1, spsr_el1
    str x1, [sp, #264]
    mrs x1, esr_el1
    str x1, [sp, #272]
    mrs x1, far_el1
    str x1, [sp, #280]
    str x0, [sp, #288]
    str xzr, [sp, #296]

    mov x0, sp
    bl exception_dispatch
    mov sp, x0

    ldr x1, [sp, #256]
    msr elr_el1, x1
    ldr x1, [sp, #264]
    msr spsr_el1, x1

    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldp x0, x1, [sp, #0]

    add sp, sp, #TRAP_FRAME_SIZE
    eret

.global trap_frame_restore
trap_frame_restore:
    mov sp, x0

    ldr x1, [sp, #256]
    msr elr_el1, x1
    ldr x1, [sp, #264]
    msr spsr_el1, x1

    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldp x0, x1, [sp, #0]

    add sp, sp, #TRAP_FRAME_SIZE
    eret
