#pragma once

#include <stdint.h>

const size_t PAGE_SIZE = 4096;

/* Index Helpers */
#define PML4_INDEX(x) (((x) >> 39) & 0x1FF)
#define PDPT_INDEX(x) (((x) >> 30) & 0x1FF)
#define PD_INDEX(x)   (((x) >> 21) & 0x1FF)
#define PT_INDEX(x)   (((x) >> 12) & 0x1FF)

#define PAGE_ALIGN(x) ((x) & ~(PAGE_SIZE - 1))

#define PTE_ADDR(x) ((x) & 0x000FFFFFFFFFF000ULL)


typedef uint64_t pte_t;

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_RW       (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_NX       (1ULL << 63)

void vmm_init(void);

void map_page(uint64_t virt, uint64_t phys, uint64_t flags);

void unmap_page(uint64_t virt);

uint64_t virt_to_phys(uint64_t virt);

pte_t* get_pte(uint64_t virt, int create);

extern pte_t* kernel_pml4;
