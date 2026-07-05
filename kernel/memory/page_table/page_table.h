#pragma once

#include <stdint.h>

#include "fs/types.h"

#define PAGE_SIZE 4096ULL
#define PAGE_TABLE_ENTRIES 512ULL
#define PTE_ADDR_MASK     UINT64_C(0x0000fffffffff000)
#define DESC_VALID        (1ULL << 0)
#define DESC_PAGE (1ULL << 1)
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
#define PTE_AP_EL1_RW (0ULL << 6)
#define PTE_AP_EL1_RO (2ULL << 6)
#define PTE_AP_EL0_RW (1ULL << 6)
#define PTE_AP_EL0_RO (3ULL << 6)
#define PTE_AP_MASK (3ULL << 6)
#define PTE_AP_SHIFT 6
#define PTE_FLAG_COW (1u << 0)

typedef struct Page {
    uint16_t refcount;
    uint16_t flags;
} Page;

void pt_init(struct Page *pages);
uint64_t kernel_phys_addr(uint64_t va);

void *alloc_page(void);
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
void *pt_seed_user_page(uint64_t *l0, uint64_t va);
void *pt_get_mapped_page(uint64_t *l0, uint64_t va);

uint64_t *initialize_kernel_page_table(void);
uint64_t *initialize_user_page_table(void);

int add_page_table_struct(uint64_t *table);
void free_page_table_struct(uint64_t *table);
void destroy_page_table(uint64_t *table);
int copy_page_table_struct(uint64_t *src_table, uint64_t *dst_table);
#define PAGE_FAULT_NOT_HANDLED 0
#define PAGE_FAULT_HANDLED 1
#define PAGE_FAULT_PERMISSION -1
#define PAGE_FAULT_ERROR -2

int load_memory_segment(uint64_t *table, ino_id_t ino_id,
                        uint64_t file_offset, uint64_t file_size,
                        uint64_t va, uint64_t pa, uint64_t mem_size,
                        uint32_t flags);
int load_segment_page_for_fault(uint64_t *table, uint64_t fault_va,
                                int instruction_fault);

/* COW helpers */
int pte_is_user(uint64_t pte);
int pte_is_writable(uint64_t pte);

void pte_set_cow_flag(uint64_t pa, uint16_t flag);
void pte_clear_cow_flag(uint64_t pa, uint16_t flag);
uint16_t pte_test_cow_flag(uint64_t pa, uint16_t flag);
uint64_t pte_make_readonly(uint64_t pte);

void inc_pte_refcount_pa(uint64_t pa);
void inc_pte_refcount_va(void *va);

void dec_pte_refcount_pa(uint64_t pa);
void dec_pte_refcount_va(void *va);

uint16_t get_pte_refcount_pa(uint64_t pa);
uint16_t get_pte_refcount_va(void *va);

void pte_make_readonly_and_mark_cow(uint64_t *pte_ptr);
void pte_clear_cow_and_make_writable(uint64_t *pte_ptr);

void tlb_invalidate_all_user(void);
