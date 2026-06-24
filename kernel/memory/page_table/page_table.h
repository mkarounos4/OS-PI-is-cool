#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PAGE_TABLE_ENTRIES 512ULL
#define PTE_ADDR_MASK     UINT64_C(0x0000fffffffff000)
#define DESC_VALID        (1ULL << 0)
#define KERNEL_VA_BASE UINT64_C(0xffff000000000000)
#define KERNEL_HEAP_START UINT64_C(0xffff800000000000)
#define KERNEL_HEAP_SIZE  UINT64_C(0x100000)
#define USER_VA_BASE UINT64_C(0x10000)
#define USER_HEAP_START UINT64_C(0x400000)
#define USER_HEAP_SIZE 16384ULL
#define USER_STACK_SIZE 8192ULL
#define USER_STACK_TOP UINT64_C(0x800000)
#define PROC_KERNEL_STACK_SIZE 8192ULL
#define PROC_KERNEL_STACK_TOP UINT64_C(0x900000)

typedef struct Page {
    uint16_t refcount;
} Page;

void pt_init(struct Page *pages);

void *alloc_page(void);
void increment_refcount();
void free_page(void *page);

uint8_t copy_phys_page(uint64_t src_pa, uint64_t dst_pa);
uint64_t table_desc(uint64_t *table);
uint64_t kernel_direct_map_va(uint64_t pa);

uint8_t pt_map_page(uint64_t *l0, uint64_t va, uint64_t pa, uint64_t attrs);
uint8_t pt_map_range(uint64_t *l0, uint64_t va_start, uint64_t pa_start,
                     uint64_t size, uint64_t attrs);
uint8_t pt_walk(uint64_t *l0, uint64_t va);
uint8_t pt_walk_user_page(uint64_t *l0, uint64_t va);
uint8_t pt_walk_kernel_page(uint64_t *l0, uint64_t va);
void *pt_seed_kernel_page(uint64_t *l0, uint64_t va);

uint64_t *initialize_kernel_page_table(void);
uint64_t *initialize_user_page_table(void);
