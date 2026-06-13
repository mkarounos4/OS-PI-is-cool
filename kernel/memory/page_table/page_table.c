#include "page_table.h"

#include <stddef.h>

#define PAGE_TABLE_ENTRIES 512ULL
#define PAGE_MASK         (PAGE_SIZE - 1ULL)
#define PTE_ADDR_MASK     UINT64_C(0x0000fffffffff000)

#define DESC_VALID        (1ULL << 0)
#define DESC_TABLE        (1ULL << 1)
#define DESC_PAGE         (1ULL << 1)

#define PTE_ATTRINDX(n)   (((uint64_t)(n) & 0x7ULL) << 2)
#define PTE_AP_EL1_RW     (0ULL << 6)
#define PTE_AP_EL1_RO     (2ULL << 6)
#define PTE_AP_EL0_RW     (1ULL << 6)
#define PTE_SH_INNER      (3ULL << 8)
#define PTE_AF            (1ULL << 10)
#define PTE_PXN           (1ULL << 53)
#define PTE_UXN           (1ULL << 54)

#define ATTR_NORMAL       PTE_ATTRINDX(1)
#define ATTR_KERNEL_RX    (ATTR_NORMAL | PTE_AP_EL1_RO | PTE_SH_INNER | PTE_AF | PTE_UXN)
#define ATTR_KERNEL_RO    (ATTR_KERNEL_RX | PTE_PXN)
#define ATTR_KERNEL_RW    (ATTR_NORMAL | PTE_AP_EL1_RW | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)
#define ATTR_USER_RW      (ATTR_NORMAL | PTE_AP_EL0_RW | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)

extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __kernel_end[];
extern uint8_t __kernel_page_pool_start[];
extern uint8_t __RAM_end[];

typedef struct FreePage {
    struct FreePage *next;
} FreePage;

static uint64_t next_free_page;
static FreePage *free_list;

static uint64_t align_down(uint64_t value) {
    return value & ~PAGE_MASK;
}

static uint64_t align_up(uint64_t value) {
    return (value + PAGE_MASK) & ~PAGE_MASK;
}

static void zero_page(void *page) {
    uint64_t *words = (uint64_t *)page;
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        words[i] = 0;
    }
}

static void page_allocator_init(void) {
    if (next_free_page != 0) {
        return;
    }

    next_free_page = align_up((uint64_t)(uintptr_t)__kernel_page_pool_start);
}

void *alloc_page(void) {
    page_allocator_init();

    void *page = NULL;
    if (free_list != NULL) {
        page = (void *)free_list;
        free_list = free_list->next;
    } else if (next_free_page + PAGE_SIZE <= (uint64_t)(uintptr_t)__RAM_end) {
        page = (void *)(uintptr_t)next_free_page;
        next_free_page += PAGE_SIZE;
    }

    if (page != NULL) {
        zero_page(page);
    }

    return page;
}

void free_page(void *page) {
    if (page == NULL) {
        return;
    }

    FreePage *free = (FreePage *)page;
    free->next = free_list;
    free_list = free;
}

uint64_t kernel_direct_map_va(uint64_t pa) {
    return KERNEL_VA_BASE | (pa & UINT64_C(0x0000ffffffffffff));
}

static uint64_t table_desc(uint64_t *table) {
    return ((uint64_t)(uintptr_t)table & PTE_ADDR_MASK) | DESC_VALID | DESC_TABLE;
}

static uint64_t *next_table(uint64_t *table, uint64_t index) {
    if ((table[index] & DESC_VALID) == 0) {
        uint64_t *next = alloc_page();
        if (next == NULL) {
            return NULL;
        }
        table[index] = table_desc(next);
    }

    return (uint64_t *)(uintptr_t)(table[index] & PTE_ADDR_MASK);
}

uint8_t pt_map_page(uint64_t *l0, uint64_t va, uint64_t pa, uint64_t attrs) {
    if (l0 == NULL || (va & PAGE_MASK) != 0 || (pa & PAGE_MASK) != 0) {
        return 0;
    }

    uint64_t l0_index = (va >> 39) & 0x1ffULL;
    uint64_t l1_index = (va >> 30) & 0x1ffULL;
    uint64_t l2_index = (va >> 21) & 0x1ffULL;
    uint64_t l3_index = (va >> 12) & 0x1ffULL;

    uint64_t *l1 = next_table(l0, l0_index);
    if (l1 == NULL) {
        return 0;
    }

    uint64_t *l2 = next_table(l1, l1_index);
    if (l2 == NULL) {
        return 0;
    }

    uint64_t *l3 = next_table(l2, l2_index);
    if (l3 == NULL) {
        return 0;
    }

    l3[l3_index] = (pa & PTE_ADDR_MASK) | (attrs & ~PTE_ADDR_MASK) |
                   DESC_VALID | DESC_PAGE;
    return 1;
}

uint8_t pt_map_range(uint64_t *l0, uint64_t va_start, uint64_t pa_start,
                     uint64_t size, uint64_t attrs) {
    if (size == 0) {
        return 1;
    }

    uint64_t va = align_down(va_start);
    uint64_t pa = align_down(pa_start);
    uint64_t end = align_up(va_start + size);

    while (va < end) {
        if (!pt_map_page(l0, va, pa, attrs)) {
            return 0;
        }
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    return 1;
}

uint8_t pt_walk(uint64_t *l0, uint64_t va) {
    void *page = alloc_page();
    if (page == NULL) {
        return 0;
    }

    return pt_map_page(l0, align_down(va), (uint64_t)(uintptr_t)page, ATTR_USER_RW);
}

static uint8_t map_kernel_section(uint64_t *l0, uint8_t *start, uint8_t *end,
                                  uint64_t attrs) {
    uint64_t pa_start = (uint64_t)(uintptr_t)start;
    uint64_t pa_end = (uint64_t)(uintptr_t)end;

    return pt_map_range(l0, kernel_direct_map_va(pa_start), pa_start,
                        pa_end - pa_start, attrs);
}

static uint8_t map_allocated_page_tables(uint64_t *l0) {
    uint64_t pool_start = (uint64_t)(uintptr_t)__kernel_page_pool_start;
    uint64_t mapped_end = 0;

    while (mapped_end != next_free_page) {
        mapped_end = next_free_page;
        if (!pt_map_range(l0, kernel_direct_map_va(pool_start), pool_start,
                          mapped_end - pool_start, ATTR_KERNEL_RW)) {
            return 0;
        }
    }

    return 1;
}

uint64_t *initialize_kernel_page_table(void) {
    uint64_t *l0 = alloc_page();
    if (l0 == NULL) {
        return NULL;
    }

    if (!map_kernel_section(l0, __text_start, __text_end, ATTR_KERNEL_RX)) {
        return NULL;
    }

    if (!map_kernel_section(l0, __rodata_start, __rodata_end, ATTR_KERNEL_RO)) {
        return NULL;
    }

    if (!map_kernel_section(l0, __data_start, __kernel_end, ATTR_KERNEL_RW)) {
        return NULL;
    }

    if (!map_allocated_page_tables(l0)) {
        return NULL;
    }

    return l0;
}

uint64_t *initialize_user_page_table(void) {
    /*
     * EL0 gets its own TTBR0 table. There is no kernel direct map here; the
     * shared high-half kernel mapping lives in TTBR1_EL1.
     */
    return alloc_page();
}
