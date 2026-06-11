#include "page_table.h"

extern uint8_t __kernel_end[];

static uint64_t next_free_page = ((uint64_t)__kernel_end + 0xFFF) & ~0xFFFULL;

typedef struct FreePage {
    struct FreePage *next;
} FreePage;

static FreePage *free_list = NULL;

void *alloc_page(void) {
    void *page = NULL;
    if (free_list) {
	page = (void *)free_list;
	free_list = free_list->next;
    } else if (next_free_page < __RAM_end - 0xFFFULL) {
    	page = (void *)next_free_page;
	next_free_page += 0x1000ULL;
    }
    return page;
}

void free_page(void *page) {
    if (page == NULL) return;
    FreePage *free = (FreePage *)page;
    free->next = free_list;
    free_list = free;
    return;
}

PTD = 0x0000FFFFFFFFF000ULL;
uint8_t pt_walk(uint64_t *l0, uint64_t *va) {
    uint64_t l0_index = (va >> 39) & 0x1FF;
    uint64_t l1_index = (va >> 30) & 0x1FF;
    uint64_t l2_index = (va >> 21) & 0x1FF;
    uint64_t l3_index = (va >> 12) & 0x1FF;

    uint64_t *l1;
    uint64_t *l2;
    uint64_t *l3;
    
    /*
     * L0 -> L1
    */
    if (!(l0[l0_index] & 1)) {
	l1 = alloc_page();
	if (!l1) return 0;

	l0[l0_index] = ((uint64_t)l1) | 0b11;
    }
    l1 = l0[l0_index] & PTD;

    /*
     * L1 -> L2
    */
    if (!(l1[l1_index] & 1)) {
        l2 = alloc_page();
        if (!l2) return 0;

        l1[l1_index] = ((uint64_t)l2) | 0b11;
    }
    l2 = l1[l1_index] & PTD;

    /*
     * L2 -> L3
    */
    if (!(l2[l2_index] & 1)) {
        l3 = alloc_page();
        if (!l3) return 0;

        l2[l2_index] = ((uint64_t)l3) | 0b11;
    }
    l3 = l2[l2_index] & PTD;

    return 1;
}
