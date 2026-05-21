#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12ULL
#define PAGE_MASK (PAGE_SIZE - 1ULL)

static inline uint64_t page_align_down(uint64_t value) {
    return value & ~PAGE_MASK;
}

static inline uint64_t page_align_up(uint64_t value) {
    return (value + PAGE_MASK) & ~PAGE_MASK;
}

static inline int page_aligned(uint64_t value) {
    return (value & PAGE_MASK) == 0;
}
