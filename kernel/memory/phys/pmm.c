#include "pmm.h"

#include <string.h>

#define PMM_MAX_MEMORY (1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PAGE_SIZE)

static uint8_t bitmap[PMM_MAX_PAGES / 8];
static uint64_t memory_base_addr;
static uint64_t memory_end_addr;
static uint64_t total_page_count;
static uint64_t used_page_count;

static inline void bitmap_set(uint64_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit) {
    bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static inline int bitmap_test(uint64_t bit) {
    return (bitmap[bit / 8] & (uint8_t)(1u << (bit % 8))) != 0;
}

static int addr_to_page_index(uint64_t addr, uint64_t *index) {
    if (addr < memory_base_addr || addr >= memory_end_addr || !page_aligned(addr)) {
        return -1;
    }

    *index = (addr - memory_base_addr) / PAGE_SIZE;
    return 0;
}

void pmm_init(uint64_t memory_base, uint64_t memory_size) {
    memory_base_addr = page_align_up(memory_base);
    memory_end_addr = page_align_down(memory_base + memory_size);
    total_page_count = (memory_end_addr - memory_base_addr) / PAGE_SIZE;

    if (total_page_count > PMM_MAX_PAGES) {
        total_page_count = PMM_MAX_PAGES;
        memory_end_addr = memory_base_addr + total_page_count * PAGE_SIZE;
    }

    memset(bitmap, 0xff, sizeof(bitmap));
    used_page_count = total_page_count;
    pmm_free_range(memory_base_addr, total_page_count * PAGE_SIZE);

    if (memory_base_addr == 0) {
        pmm_reserve_range(0, PAGE_SIZE);
    }
}

void pmm_reserve_range(uint64_t base, uint64_t size) {
    uint64_t start = page_align_down(base);
    uint64_t end = page_align_up(base + size);

    if (end <= memory_base_addr || start >= memory_end_addr) {
        return;
    }

    if (start < memory_base_addr) {
        start = memory_base_addr;
    }
    if (end > memory_end_addr) {
        end = memory_end_addr;
    }

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t index;
        if (addr_to_page_index(addr, &index) == 0 && !bitmap_test(index)) {
            bitmap_set(index);
            used_page_count++;
        }
    }
}

void pmm_free_range(uint64_t base, uint64_t size) {
    uint64_t start = page_align_up(base);
    uint64_t end = page_align_down(base + size);

    if (end <= memory_base_addr || start >= memory_end_addr) {
        return;
    }

    if (start < memory_base_addr) {
        start = memory_base_addr;
    }
    if (end > memory_end_addr) {
        end = memory_end_addr;
    }

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t index;
        if (addr_to_page_index(addr, &index) == 0 && bitmap_test(index)) {
            bitmap_clear(index);
            used_page_count--;
        }
    }
}

void *alloc_page(void) {
    for (uint64_t i = 0; i < total_page_count; i++) {
        if (!bitmap_test(i)) {
            uint64_t addr = memory_base_addr + i * PAGE_SIZE;
            bitmap_set(i);
            used_page_count++;
            memset((void *)(uintptr_t)addr, 0, PAGE_SIZE);
            return (void *)(uintptr_t)addr;
        }
    }

    return NULL;
}

void *alloc_pages(size_t count) {
    if (count == 0) {
        return NULL;
    }

    for (uint64_t i = 0; i + count <= total_page_count; i++) {
        int available = 1;
        for (uint64_t j = 0; j < count; j++) {
            if (bitmap_test(i + j)) {
                available = 0;
                i += j;
                break;
            }
        }

        if (!available) {
            continue;
        }

        for (uint64_t j = 0; j < count; j++) {
            bitmap_set(i + j);
        }
        used_page_count += count;

        uint64_t addr = memory_base_addr + i * PAGE_SIZE;
        memset((void *)(uintptr_t)addr, 0, PAGE_SIZE * count);
        return (void *)(uintptr_t)addr;
    }

    return NULL;
}

void free_page(void *page) {
    uint64_t index;
    uint64_t addr = (uint64_t)(uintptr_t)page;

    if (addr_to_page_index(addr, &index) != 0 || !bitmap_test(index)) {
        return;
    }

    bitmap_clear(index);
    used_page_count--;
}

uint64_t pmm_total_pages(void) {
    return total_page_count;
}

uint64_t pmm_used_pages(void) {
    return used_page_count;
}

uint64_t pmm_memory_base(void) {
    return memory_base_addr;
}

uint64_t pmm_memory_end(void) {
    return memory_end_addr;
}
