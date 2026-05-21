#include "paging.h"

void page_table_init(void *ustack, void *uprogram, void *memory, size_t memory_size) {
    (void)ustack;
    (void)uprogram;

    vm_init((uint64_t)(uintptr_t)memory, (uint64_t)memory_size);
    vm_enable_kernel_mmu();
}
