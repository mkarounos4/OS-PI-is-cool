#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define KERNEL_VA_BASE UINT64_C(0xffff000000000000)

void *alloc_page(void);
void free_page(void *page);

uint64_t kernel_direct_map_va(uint64_t pa);

uint8_t pt_map_page(uint64_t *l0, uint64_t va, uint64_t pa, uint64_t attrs);
uint8_t pt_map_range(uint64_t *l0, uint64_t va_start, uint64_t pa_start,
                     uint64_t size, uint64_t attrs);
uint8_t pt_walk(uint64_t *l0, uint64_t va);

uint64_t *initialize_kernel_page_table(void);
uint64_t *initialize_user_page_table(void);
