.section .text.mmu, "ax"
.align 2

.global initialize_mmu_asm
.type initialize_mmu_asm, %function
initialize_mmu_asm:
    // x0 = TTBR0_EL1, x1 = TTBR1_EL1, x2 = TCR_EL1, x3 = MAIR_ELE1

    msr mair_el1, x3

    dsb ishst
    tlbi vmalle1 // TLB invalidate by VMID
    dsb ish

    // Initialize TCR and table start addresses
    msr ttbr0_el1, x0
    msr ttbr1_el1, x1
    msr tcr_el1, x2
    isb

    // Set MMU enable bit to 1
    mrs x4, sctlr_el1
    orr x4, x4, #1
    msr sctlr_el1, x4
    isb

    ret
.size initialize_mmu_asm, . - initialize_mmu_asm
