#pragma once

#include <stdint.h>

#include "memory/page.h"

typedef uint64_t pte_t;

#define VM_USER_CODE_BASE  UINT64_C(0x0000000000400000)
#define VM_USER_HEAP_BASE  UINT64_C(0x0000000001000000)
#define VM_USER_STACK_TOP  UINT64_C(0x000000003ffff000)

#define VM_FLAG_READ       (UINT64_C(1) << 0)
#define VM_FLAG_WRITE      (UINT64_C(1) << 1)
#define VM_FLAG_EXEC       (UINT64_C(1) << 2)
#define VM_FLAG_USER       (UINT64_C(1) << 3)
#define VM_FLAG_DEVICE     (UINT64_C(1) << 4)

struct address_space {
    pte_t *root_table;
    uint64_t ttbr0_phys;
};

void vm_init(uint64_t memory_base, uint64_t memory_size);
void vm_enable_kernel_mmu(void);
int vm_is_enabled(void);

struct address_space *vm_kernel_address_space(void);
struct address_space *vm_create_address_space(void);
int vm_map_page(struct address_space *as, uint64_t va, uint64_t pa, uint64_t flags);
int vm_map_range(struct address_space *as, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
int vm_unmap_page(struct address_space *as, uint64_t va);
uint64_t vm_virt_to_phys(struct address_space *as, uint64_t va);
uint64_t vm_debug_get_pte(struct address_space *as, uint64_t va);
void vm_switch_address_space(struct address_space *as);

int validate_user_range(struct address_space *as, uint64_t va, uint64_t len, int write);
int copy_from_user(struct address_space *as, void *dst, uint64_t user_src, uint64_t len);
int copy_to_user(struct address_space *as, uint64_t user_dst, const void *src, uint64_t len);
int copy_string_from_user(struct address_space *as, char *dst, uint64_t user_src, uint64_t max_len);
