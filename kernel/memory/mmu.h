#pragma once

#include <stdint.h>

#define ADDRESS_SIZE_FAULT 0x00
#define TRANSLATION_FAULT 0x04
#define ACCESS_FLAG_FAULT 0x08
#define PERMISSION_FAULT 0x0c
#define SYNC_EXT_ABORT_NON_WALK 0x10
#define SYNC_TAG_CHECK_FAULT 0x11
#define SYNC_EXT_ABORT_WALK 0x14
#define SYNC_PARITY_ECC_ERR_ACCESS 0x18
#define SYNC_PARITY_ECC_ERR_WALK 0x1c
#define ALIGNMENT_FAULT 0x21
#define TLB_CONFLICT_ABORT 0x30
#define UNSUPPORTED_ATOMIC_HW_UPDATE 0x31
#define UNSUPPORTED_ATOMIC_ACCESS 0x35

#define ESR_WNR   (1UL << 6)   // 1 = write, 0 = read
#define ESR_S1PTW (1UL << 7)   // fault during stage-1 page-table walk
#define ESR_CM    (1UL << 8)   // cache maintenance fault
#define ESR_EA    (1UL << 9)   // external abort type info
#define ESR_FNV   (1UL << 10)  // FAR not valid
#define ESR_ISV   (1UL << 24)  // instruction syndrome valid

void handle_instruction_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr);
void handle_data_abort(uint64_t fsc, uint64_t far, uint64_t elr, uint64_t esr);
void initialize_vm(void);
void initialize_mmu(uint64_t ttbr0_el1, uint64_t ttbr1_el1);
void install_kernel_page_table(void);
