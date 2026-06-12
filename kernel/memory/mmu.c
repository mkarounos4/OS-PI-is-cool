#include "mmu.h"

#define TCR_T0SZ_48BIT      (16ULL << 0)
#define TCR_IRGN0_WB_RA_WA  (1ULL << 8)
#define TCR_ORGN0_WB_RA_WA  (1ULL << 10)
#define TCR_SH0_INNER       (3ULL << 12)
#define TCR_TG0_4K          (0ULL << 14)

#define TCR_T1SZ_48BIT      (16ULL << 16)
#define TCR_IRGN1_WB_RA_WA  (1ULL << 24)
#define TCR_ORGN1_WB_RA_WA  (1ULL << 26)
#define TCR_SH1_INNER       (3ULL << 28)
#define TCR_TG1_4K          (2ULL << 30)

#define TCR_IPS_36BIT       (1ULL << 32)

static uint64_t ttbr1_el1;

extern void initialize_mmu_asm(uint64_t ttbr0_el1, uint64_t ttbr1_el1,
                               uint64_t tcr_el1, uint64_t mair_el1);

void intialize_vm() {
    ttbr1_el1 = (uint64_t) alloc_page();
    initialize_mmu();
}

void initialize_mmu() {
    uint64_t mair_el1 = 0x000000000000ff00;

    uint64_t tcr =
        TCR_T0SZ_48BIT |
        TCR_IRGN0_WB_RA_WA |
        TCR_ORGN0_WB_RA_WA |
        TCR_SH0_INNER |
        TCR_TG0_4K |

        TCR_T1SZ_48BIT |
        TCR_IRGN1_WB_RA_WA |
        TCR_ORGN1_WB_RA_WA |
        TCR_SH1_INNER |
        TCR_TG1_4K |

        TCR_IPS_36BIT;

    initialize_mmu_asm(0, ttbr1_el1, tcr, mair_el1);
}

void handle_instruction_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr) {
    if (fsc >= ADDRESS_SIZE_FAULT && fsc < ADDRESS_SIZE_FAULT + 4) {
        fatal_exception("Instruction Abort: Address size fault");
    } else if (fsc < TRANSLATION_FAULT + 4) {
        fatal_exception("Instruction Abort: translation fault");
        break;
    } else if (fsc < ACCESS_FLAG_FAULT + 4) {
        fatal_exception("Instruction Abort: access flag set to 0");
    } else if (fsc < PERMISSION_FAULT + 4) {
        fatal_exception("Instruction Abort: Permission fault");
        break;
    } else if (fsc == SYNC_EXT_ABORT_NON_WALK) {
        fatal_exception("Instruction Abort: synchronous external abort");
    } else if (fsc >= SYNC_EXT_ABORT_WALK && fs < SYNC_EXT_ABORT_WALK + 4) {
        fatal_exception("Synchronous external abort on page walk");
    } else if (fsc == SYNC_PARITY_ECC_ERR_ACCESS) {
        fatal_exception("Synchronous parity ECC ERR access");
    } else if (fsc >= SYNC_PARITY_ECC_ERR_WALK && fs < SYNC_PARITY_ECC_ERR_WALK + 4) {
        fatal_exception("Synchronous parity ECC ERR on Page Walk");
    } else if (fsc == TLB_CONFLICT_ABORT) {
        fatal_exception("Instruction Abort: TLB Conflict Abort");
    } else if (fsc == UNSUPORTED_ATOMIC_HW_UPDATE) {
        fatal_exception("Instruction Abort: Unsupported atomic hardware update");
    } else {
        fatal_exception("Instruction Abort: Unknown fsc.");
    }
}

void handle_data_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr) {
    bool is_write = (esr & ESR_WNR) != 0;
    bool far_valid = (esr & ESR_FNV) == 0;
    bool during_table_walk = (esr & ESR_S1PTW) != 0;
    bool cache_maintenance = (esr & ESR_CM) != 0;
    bool external_abort_info = (esr & ESR_EA) != 0;

    (void)is_write;
    (void)during_table_walk;
    (void)cache_maintenance;
    (void)external_abort_info;

    if (fsc >= ADDRESS_SIZE_FAULT && fsc < ADDRESS_SIZE_FAULT + 4) {
        fatal_exception("Data Abort: Address size fault");
    } else if (fsc < TRANSLATION_FAULT + 4) {
        if (!far_valid) {
            fatal_exception("Data Abort: Translation Fault wiht Invalid FAR");
        }
        
        uint64_t table_base_addr = 0;
        if ((far >> 48) == 0xFFFF) {
            table_base_addr = ttbr1_el1;
        } else {
            pcb_t *curr_proc = get_curr_process();
            if (curr_proc == NULL) {
                fatal_exception("Data Abort: Translation fault user address with no process");
            }

            tablel_base_addr = curr_proc->ctx.ttbr0_el1;
        }

        if (!pt_walk(table_base_addr, far)) {
            fatal_exception("Data Abort: Translation Fault, failed to allocate new page");
        }
        break;
    } else if (fsc < ACCESS_FLAG_FAULT + 4) {
        if (!far_valid) {
            fatal_exception("Data Abort: Access Flag = 0 with Invalid FAR");
        }

        fatal_exception("Data Abort: access flag set to 0");
    } else if (fsc < PERMISSION_FAULT + 4) {
        fatal_exception("Data Abort: Permission fault");
        break;
    } else if (fsc == SYNC_EXT_ABORT_NON_WALK) {
        fatal_exception("Data Abort: synchronous external abort");
    } else if (fsc == SYNC_TAG_CHECK_FAULT) {
        fatal_exception("Data Abort: Synchronous tag check fault");
    } else if (fsc >= SYNC_EXT_ABORT_WALK && fs < SYNC_EXT_ABORT_WALK + 4) {
        fatal_exception("Data Abort: Synchronous external abort on page walk");
    } else if (fsc == SYNC_PARITY_ECC_ERR_ACCESS) {
        fatal_exception("Data Abort: Synchronous parity ECC ERR access");
    } else if (fsc >= SYNC_PARITY_ECC_ERR_WALK && fs < SYNC_PARITY_ECC_ERR_WALK + 4) {
        fatal_exception("Synchronous parity ECC ERR on Page Walk");
    } else if (fsc == ALIGNMENT_FAULT) {
        fatal_exception("Data Abort: misaligned data access");
    } else if (fsc == TLB_CONFLICT_ABORT) {
        fatal_exception("Data Abort: TLB Conflict Abort");
    } else if (fsc == UNSUPPORTED_ATOMIC_HW_UPDATE) {
        fatal_exception("Data Abort: Unsupported atomic hardware update");
    } else if (fsc == UNSUPPORTED_ATOMIC_ACCESS) {
        fatal_exception("Data Abort: unsupported atomic access");
    } else {
        fatal_exception("Instruction Abort: Unknown fsc.");
    }
}
