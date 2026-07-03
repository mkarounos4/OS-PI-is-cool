#include "mmu.h"

#include <stdbool.h>
#include <stdint.h>

#include "memory/page_table/page_table.h"
#include "scheduler/scheduler.h"
#include "traps/traps.h"

#define BOOT_TEXT __attribute__((section(".text.boot")))
#define BOOT_PAGE_TABLE __attribute__((section(".bss.boot_pgtables"), aligned(4096)))

#define PAGE_TABLE_ENTRIES 512ULL
#define L1_BLOCK_SIZE      UINT64_C(0x40000000)
#define TABLE_ADDR_MASK    UINT64_C(0x0000fffffffff000)
#define BLOCK_ADDR_MASK    UINT64_C(0x0000ffffc0000000)

#define DESC_VALID         (1ULL << 0)
#define DESC_TABLE         (1ULL << 1)
#define DESC_BLOCK         (0ULL << 1)
#define PTE_ATTR_NORMAL    (1ULL << 2)
#define PTE_AF             (1ULL << 10)
#define PTE_SH_INNER       (3ULL << 8)
#define PTE_PXN            (1ULL << 53)
#define PTE_UXN            (1ULL << 54)

#define BOOT_NORMAL_BLOCK  (DESC_VALID | DESC_BLOCK | PTE_ATTR_NORMAL | PTE_AF | PTE_SH_INNER)
#define BOOT_DEVICE_BLOCK  (DESC_VALID | DESC_BLOCK | PTE_AF | PTE_PXN | PTE_UXN)

#define L1_INDEX_QEMU_LOCAL 1ULL
#define L1_INDEX_RPI5_PCIE   64ULL
#define L1_INDEX_RPI_GIC    65ULL
#define L1_INDEX_RPI5_RP1_PERIPH 112ULL
#define L1_INDEX_RPI5_RP1_MSIX   126ULL

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

#define TCR_IPS_40BIT       (2ULL << 32)
#define ESR_EC_SHIFT        26ULL
#define ESR_EC_MASK         UINT64_C(0x3f)
#define ESR_EC_DABT_LOWER   UINT64_C(0x24)

static uint64_t ttbr1_el1;
static uint64_t boot_ttbr0_l0[PAGE_TABLE_ENTRIES] BOOT_PAGE_TABLE;
static uint64_t boot_ttbr0_l1[PAGE_TABLE_ENTRIES] BOOT_PAGE_TABLE;
static uint64_t boot_ttbr1_l0[PAGE_TABLE_ENTRIES] BOOT_PAGE_TABLE;
static uint64_t boot_ttbr1_l1[PAGE_TABLE_ENTRIES] BOOT_PAGE_TABLE;

extern void initialize_mmu_asm(uint64_t ttbr0_el1, uint64_t ttbr1_el1,
                               uint64_t tcr_el1, uint64_t mair_el1);

static void BOOT_TEXT zero_table(uint64_t *table) {
    volatile uint64_t *entries = table;

    for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        entries[i] = 0;
    }
}

static uint64_t BOOT_TEXT make_table_desc(uint64_t *table) {
    return ((uint64_t)(uintptr_t)table & TABLE_ADDR_MASK) |
           DESC_VALID | DESC_TABLE;
}

static uint64_t BOOT_TEXT make_l1_block_desc(uint64_t pa, uint64_t attrs) {
    return (pa & BLOCK_ADDR_MASK) | attrs;
}

static void BOOT_TEXT map_l1_block(uint64_t *l1, uint64_t index,
                                   uint64_t attrs) {
    volatile uint64_t *entries = l1;
    entries[index] = make_l1_block_desc(index * L1_BLOCK_SIZE, attrs);
}

static void BOOT_TEXT initialize_boot_tables(void) {
    zero_table(boot_ttbr0_l0);
    zero_table(boot_ttbr0_l1);
    zero_table(boot_ttbr1_l0);
    zero_table(boot_ttbr1_l1);

    boot_ttbr0_l0[0] = make_table_desc(boot_ttbr0_l1);
    boot_ttbr1_l0[0] = make_table_desc(boot_ttbr1_l1);

    map_l1_block(boot_ttbr0_l1, 0, BOOT_NORMAL_BLOCK);
    map_l1_block(boot_ttbr0_l1, L1_INDEX_QEMU_LOCAL, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr0_l1, L1_INDEX_RPI5_PCIE, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr0_l1, L1_INDEX_RPI_GIC, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr0_l1, L1_INDEX_RPI5_RP1_PERIPH, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr0_l1, L1_INDEX_RPI5_RP1_MSIX, BOOT_DEVICE_BLOCK);

    map_l1_block(boot_ttbr1_l1, 0, BOOT_NORMAL_BLOCK);
    map_l1_block(boot_ttbr1_l1, L1_INDEX_QEMU_LOCAL, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr1_l1, L1_INDEX_RPI5_PCIE, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr1_l1, L1_INDEX_RPI_GIC, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr1_l1, L1_INDEX_RPI5_RP1_PERIPH, BOOT_DEVICE_BLOCK);
    map_l1_block(boot_ttbr1_l1, L1_INDEX_RPI5_RP1_MSIX, BOOT_DEVICE_BLOCK);
}

void BOOT_TEXT initialize_vm(void) {
    initialize_boot_tables();
    initialize_mmu((uint64_t)(uintptr_t)boot_ttbr0_l0,
                   (uint64_t)(uintptr_t)boot_ttbr1_l0);

    while (1) {
        asm volatile("wfe");
    }
}

void BOOT_TEXT initialize_mmu(uint64_t ttbr0_el1, uint64_t ttbr1_el1_value) {
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

        TCR_IPS_40BIT;

    initialize_mmu_asm(ttbr0_el1, ttbr1_el1_value, tcr, mair_el1);
}

static uint64_t table_phys_addr(uint64_t *table) {
    return (uint64_t)(uintptr_t)table & TABLE_ADDR_MASK;
}

static uint8_t is_in_range(uint64_t va, uint64_t start, uint64_t size) {
    return va >= start && va < start + size;
}

static void invalidate_all_stage1_tlbs(void) {
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory");
}

void install_kernel_page_table(void) {
    uint64_t *kernel_l0 = initialize_kernel_page_table();
    if (kernel_l0 == NULL) {
        fatal_exception("failed to initialize final kernel page table");
        return;
    }

    ttbr1_el1 = table_phys_addr(kernel_l0);
    asm volatile(
        "dsb ishst\n"
        "msr ttbr1_el1, %0\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        :
        : "r"(ttbr1_el1)
        : "memory");
}

static void dump_mmu_state(void);

void handle_instruction_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr) {
    (void)far;
    (void)elr;
    (void)esr;

    if (fsc < ADDRESS_SIZE_FAULT + 4) {
        fatal_exception("Instruction Abort: Address size fault");
    } else if (fsc < TRANSLATION_FAULT + 4) {
        fatal_exception("Instruction Abort: translation fault");
    } else if (fsc < ACCESS_FLAG_FAULT + 4) {
        fatal_exception("Instruction Abort: access flag set to 0");
    } else if (fsc < PERMISSION_FAULT + 4) {
        dump_mmu_state();
        fatal_exception("Instruction Abort: Permission fault");
    } else if (fsc == SYNC_EXT_ABORT_NON_WALK) {
        fatal_exception("Instruction Abort: synchronous external abort");
    } else if (fsc >= SYNC_EXT_ABORT_WALK && fsc < SYNC_EXT_ABORT_WALK + 4) {
        fatal_exception("Synchronous external abort on page walk");
    } else if (fsc == SYNC_PARITY_ECC_ERR_ACCESS) {
        fatal_exception("Synchronous parity ECC ERR access");
    } else if (fsc >= SYNC_PARITY_ECC_ERR_WALK && fsc < SYNC_PARITY_ECC_ERR_WALK + 4) {
        fatal_exception("Synchronous parity ECC ERR on Page Walk");
    } else if (fsc == TLB_CONFLICT_ABORT) {
        fatal_exception("Instruction Abort: TLB Conflict Abort");
    } else if (fsc == UNSUPPORTED_ATOMIC_HW_UPDATE) {
        fatal_exception("Instruction Abort: Unsupported atomic hardware update");
    } else {
        fatal_exception("Instruction Abort: Unknown fsc.");
    }
}

static void dump_mmu_state(void)
{
    uint64_t current_pc;
    uint64_t sp;
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t sctlr;
    uint64_t vbar;
    uint64_t el;
    uint64_t esr;
    uint64_t far;
    uint64_t elr;

    asm volatile("adr %0, ." : "=r"(current_pc));
    asm volatile("mov %0, sp" : "=r"(sp));

    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    asm volatile("mrs %0, ttbr1_el1" : "=r"(ttbr1));
    asm volatile("mrs %0, tcr_el1" : "=r"(tcr));
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));

    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, far_el1" : "=r"(far));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));

    printf("CurrentEL : %x\n", el);
    printf("PC        : %x\n", current_pc);
    printf("SP        : %x\n", sp);

    printf("TTBR0_EL1 : %x\n", ttbr0);
    printf("TTBR1_EL1 : %x\n", ttbr1);

    printf("TCR_EL1   : %x\n", tcr);
    printf("SCTLR_EL1 : %x\n", sctlr);

    printf("VBAR_EL1  : %x\n", vbar);

    printf("ELR_EL1   : %x\n", elr);
    printf("ESR_EL1   : %x\n", esr);
    printf("FAR_EL1   : %x\n", far);
}

extern char _stack_top;
extern char __kernel_start;
extern char __kernel_end;

void dump_symbols(void)
{
    int local;

    printf("&local         : %x\n", (uint64_t)&local);
    printf("_stack_top     : %x\n", (uint64_t)&_stack_top);
    printf("__kernel_start : %x\n", (uint64_t)&__kernel_start);
    printf("__kernel_end   : %x\n", (uint64_t)&__kernel_end);
}

void handle_data_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr) {
    (void)elr;

    bool is_write = (esr & ESR_WNR) != 0;
    bool far_valid = (esr & ESR_FNV) == 0;
    bool during_table_walk = (esr & ESR_S1PTW) != 0;
    bool cache_maintenance = (esr & ESR_CM) != 0;
    bool external_abort_info = (esr & ESR_EA) != 0;

    uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    bool from_lower_el = ec == ESR_EC_DABT_LOWER;

    (void)is_write;
    (void)during_table_walk;
    (void)cache_maintenance;
    (void)external_abort_info;

    if (fsc < ADDRESS_SIZE_FAULT + 4) {
        fatal_exception("Data Abort: Address size fault");
    } else if (fsc < TRANSLATION_FAULT + 4) {
        if (!far_valid) {
            fatal_exception("Data Abort: Translation Fault wiht Invalid FAR");
        }
        
        uint64_t table_base_addr = 0;
        uint8_t mapped = 0;
        if ((far >> 48) == 0xFFFF) {
            if (!is_in_range(far, KERNEL_HEAP_START, KERNEL_HEAP_SIZE)) {
                fatal_exception("Data Abort: Translation fault outside kernel lazy ranges");
                return;
            }

            table_base_addr = kernel_direct_map_va(ttbr1_el1);
            mapped = pt_walk_kernel_page((uint64_t *)(uintptr_t)table_base_addr, far);
        } else {
            pcb_t *curr_proc = get_curr_process();
            if (curr_proc == NULL) {
                fatal_exception("Data Abort: Translation fault user address with no process");
                return;
            }

            table_base_addr = kernel_direct_map_va(curr_proc->ctx.ttbr0_el1);
            if (is_in_range(far, USER_HEAP_START, USER_HEAP_SIZE) ||
                is_in_range(far, USER_STACK_TOP - USER_STACK_SIZE,
                            USER_STACK_SIZE)) {
                mapped = pt_walk_user_page((uint64_t *)(uintptr_t)table_base_addr,
                                           far);
            } else if (!from_lower_el &&
                       is_in_range(far,
                                   PROC_KERNEL_STACK_TOP - PROC_KERNEL_STACK_SIZE,
                                   PROC_KERNEL_STACK_SIZE)) {
                mapped = pt_walk_kernel_page((uint64_t *)(uintptr_t)table_base_addr,
                                             far);
            } else {
                fatal_exception("Data Abort: Translation fault outside process lazy ranges");
                return;
            }
        }

        if (!mapped) {
            fatal_exception("Data Abort: Translation Fault, failed to allocate new page");
            return;
        }

        invalidate_all_stage1_tlbs();
    } else if (fsc < ACCESS_FLAG_FAULT + 4) {
        if (!far_valid) {
            fatal_exception("Data Abort: Access Flag = 0 with Invalid FAR");
        }

        fatal_exception("Data Abort: access flag set to 0");
    } else if (fsc < PERMISSION_FAULT + 4) {
        // permission fault: attempt to handle COW write fault
        if (!far_valid) {
            fatal_exception("Data Abort: Permission fault, invalid far"); 
            return;
        }
        if (!is_write) {
            fatal_exception("Data Abort: Permission fault, read");
            return;
        }
        if ((far >> 48) == 0xFFFF) {
            fatal_exception("Data Abort: Permission fault, kernel addres"); 
            return;
        }
        pcb_t *curr_proc = get_curr_process();
        if (curr_proc == NULL) {
            fatal_exception("Data Abort: Permission fault, user address; no process"); 
            return;
        }

        // walk page table
        uint64_t table_base_va = kernel_direct_map_va(curr_proc->ctx.ttbr0_el1);
        uint64_t *l0 = (uint64_t *)(uintptr_t)table_base_va;

        uint64_t va = far;
        uint64_t l0_index = (va >> 39) & 0x1ffULL;
        uint64_t l1_index = (va >> 30) & 0x1ffULL;
        uint64_t l2_index = (va >> 21) & 0x1ffULL;
        uint64_t l3_index = (va >> 12) & 0x1ffULL;

        if ((l0[l0_index] & DESC_VALID) == 0) {
            fatal_exception("Data Abort COW: missing L0 entry"); 
            return;
        }
        uint64_t *l1 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l0[l0_index] & PTE_ADDR_MASK);
        if ((l1[l1_index] & DESC_VALID) == 0) {
            fatal_exception("Data Abort COW: missing L1 entry"); 
            return;
        }
        uint64_t *l2 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l1[l1_index] & PTE_ADDR_MASK);
        if ((l2[l2_index] & DESC_VALID) == 0) {
            fatal_exception("Data Abort COW: missing L2 entry"); 
            return;
        }
        uint64_t *l3 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l2[l2_index] & PTE_ADDR_MASK);
        if ((l3[l3_index] & DESC_VALID) == 0) {
            fatal_exception("Data Abort COW: missing L3 entry"); 
            return;
        }
        uint64_t *pte_ptr = &l3[l3_index];
        uint64_t pte_val = *pte_ptr;

        // ensure page descriptor and user mapping
        if ((pte_val & DESC_VALID) == 0) {
            fatal_exception("Data Abort COW: not a page descriptor"); 
            return;
        }
        if (!pte_is_user(pte_val)) {
            fatal_exception("Data Abort COW: permission fault on non-user page"); 
            return;
        }

        uint64_t pa = pte_val & PTE_ADDR_MASK;

        if (!pte_test_cow_flag(pa, PTE_FLAG_COW)) {
            fatal_exception("Data Abort COW: write to non-COW read-only page"); 
            return;
        }

        uint16_t refcnt = get_pte_refcount_pa(pa);
        if (refcnt > 1) { // multiple page owners
            // allocate new page and copy
            void *new_page_va = alloc_page();
            if (new_page_va == NULL) {
                fatal_exception("Data Abort COW: out of memory allocating copy page"); 
                return;
            }

            uint64_t new_pa = kernel_phys_addr((uint64_t)(uintptr_t)new_page_va);
            if (!copy_phys_page(pa, new_pa)) {
                free_page(new_page_va);
                fatal_exception("Data Abort COW: failed to copy physical page");
                return;
            }

            // decrement refcount for shared page
            dec_pte_refcount_pa(pa);

            // update PTE, preserve attrs, make writable, clear COW metadata
            uint64_t attrs = pte_val & ~PTE_ADDR_MASK;
            *pte_ptr = (new_pa & PTE_ADDR_MASK) | attrs;
            pte_clear_cow_and_make_writable(pte_ptr);

            invalidate_all_stage1_tlbs();
            return;
        } else { // sole owner of page
            pte_clear_cow_and_make_writable(pte_ptr);
            pte_clear_cow_flag(pa, PTE_FLAG_COW);
            invalidate_all_stage1_tlbs();
            return;
        }
    } else if (fsc == SYNC_EXT_ABORT_NON_WALK) {
        fatal_exception("Data Abort: synchronous external abort");
    } else if (fsc == SYNC_TAG_CHECK_FAULT) {
        fatal_exception("Data Abort: Synchronous tag check fault");
    } else if (fsc >= SYNC_EXT_ABORT_WALK && fsc < SYNC_EXT_ABORT_WALK + 4) {
        fatal_exception("Data Abort: Synchronous external abort on page walk");
    } else if (fsc == SYNC_PARITY_ECC_ERR_ACCESS) {
        fatal_exception("Data Abort: Synchronous parity ECC ERR access");
    } else if (fsc >= SYNC_PARITY_ECC_ERR_WALK && fsc < SYNC_PARITY_ECC_ERR_WALK + 4) {
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
