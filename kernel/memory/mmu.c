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

extern void initialize_mmu_asm(uint64_t ttbr0_el1, uint64_t ttbr1_el1,
                               uint64_t tcr_el1, uint64_t mair_el1);

void initialize_mmu(uint64_t ttbr0_el1, uint64_t ttbr1_el1) {
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

    initialize_mmu_asm(ttbr0_el1, ttbr1_el1, tcr, mair_el1);
}
