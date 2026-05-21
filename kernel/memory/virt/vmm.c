#include "vmm.h"
#include "memory/phys/pmm.h"
#include <string.h>

pte_t* kernel_pml4 = NULL;
pte_t* next = (pte_t*)PTE_ADDR(entry);


void vmm_init(void)
{
    kernel_pml4 = alloc_page();

    memset(kernel_pml4, 0, PAGE_SIZE);
}

/*
 * Walk through page table to get pte address
 * PML4 -> PDPT -> PD -> PT
*/
pte_t* get_pte(uint64_t virt, int create) {
    uint64_t pml4_i = PML4_INDEX(virt);
    uint64_t pdpt_i = PDPT_INDEX(virt);
    uint64_t pd_i   = PD_INDEX(virt);
    uint64_t pt_i   = PT_INDEX(virt);

    pte_t* pdpt;
    pte_t* pd;
    pte_t* pt;

    //
    // PML4 -> PDPT
    //
    if (!(kernel_pml4[pml4_i] & PAGE_PRESENT)) {

        if (!create)
            return NULL;

        pdpt = pmm_alloc_page();

        memset(pdpt, 0, PAGE_SIZE);

        kernel_pml4[pml4_i] =
            (uint64_t)pdpt |
            PAGE_PRESENT |
            PAGE_RW;
    }

    pdpt = (pte_t*)PTE_ADDR(
        kernel_pml4[pml4_i]
    );

    //
    // PDPT -> PD
    //
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {

        if (!create)
            return NULL;

        pd = pmm_alloc_page();

        memset(pd, 0, PAGE_SIZE);

        pdpt[pdpt_i] =
            (uint64_t)pd |
            PAGE_PRESENT |
            PAGE_RW;
    }

    pd = (pte_t*)PTE_ADDR(
        pdpt[pdpt_i]
    );

    //
    // PD -> PT
    //
    if (!(pd[pd_i] & PAGE_PRESENT)) {

        if (!create)
            return NULL;

        pt = pmm_alloc_page();

        memset(pt, 0, PAGE_SIZE);

        pd[pd_i] =
            (uint64_t)pt |
            PAGE_PRESENT |
            PAGE_RW;
    }

    pt = (pte_t*)PTE_ADDR(
        pd[pd_i]
    );

    //
    // Return final PTE
    //
    return &pt[pt_i];
}

/*
 * Gets the pte of a virtual address and stores a desired
 * physical address there with given flags
*/
void map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    pte_t* pte = get_pte(virt, 1);

    *pte = PAGE_ALIGN(phys) | flags | PAGE_PRESENT;
}

/* 
 * Invalidates old page mappings
*/
static inline void invlpg(void* addr) {
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(addr)
        : "memory"
    );
}

/*
 * Removes physical address from the
 * virtual pte
*/
void vmm_unmap_page(uint64_t virt) {
    pte_t* pte =
        vmm_get_pte(virt, 0);

    if (!pte)
        return;

    *pte = 0;

    invlpg((void*)virt);
}

/*
 * Converts a virtual address to a physical one
*/
uint64_t vmm_virt_to_phys(uint64_t virt) {
    pte_t* pte = get_pte(virt, 0);

    if (!pte) return 0;

    if (!(*pte & PAGE_PRESENT)) return 0;

    return PTE_ADDR(*pte) | (virt & 0xFFF);
}
