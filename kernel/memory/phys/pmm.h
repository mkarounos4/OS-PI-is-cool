#pragma once

#include <stddef.h>
#include <stdint.h>

#include "memory/page.h"

void pmm_init(uint64_t memory_base, uint64_t memory_size);
void pmm_reserve_range(uint64_t base, uint64_t size);
void pmm_free_range(uint64_t base, uint64_t size);

void *alloc_page(void);
void *alloc_pages(size_t count);
void free_page(void *page);

uint64_t pmm_total_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_memory_base(void);
uint64_t pmm_memory_end(void);
