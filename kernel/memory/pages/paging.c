#include "paging.h"
#include "kernel.c"
#include "uart/uart.h"
#include "memory/malloc.h"
#include <stdint.h>
#include <string.h>

static const size_t PAGE_SIZE = 4096;
static const size_t PTE_COUNT = MAX_MEMORY / PAGE_SIZE; 
static const size_t PDE_COUNT = PTE_COUNT / 512; // PTE_COUNT / 512

static const uint64_t VA_START = 0xFFFFFFFFFFE00000;

// pte struct from lecture 13
struct page_entry {
	uint64_t present:1; // Bit P
	uint64_t writable:1; // Bit R/W
	uint64_t user_mode:1; // Bit U/S
	uint64_t reserved:9;
	uint64_t page_address:40; // 40+12 = 52-bit physical address (max)
	uint64_t avail:7; // reserved, should be 0
	uint64_t pke:4; // no MPK/PKE, should be 0
	uint64_t nonexecute:1;
};

void *page_table = NULL; /* TODO: Must be initialized to the page table address */
void *user_stack = NULL; /* TODO: Must be initialized to a user stack virtual address */
void *user_program = NULL; /* TODO: Must be initialized to a user program virtual address */

void map_kernel_region(void) {
    extern uint64_t kernel_start;
    extern uint64_t kernel_end;

    uint64_t start = (uint64_t)&kernel_start;
    uint64_t end   = (uint64_t)&kernel_end;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
	// identity virt/phys mapping for kernel
        map_page(addr, addr, PAGE_PRESENT | PAGE_RW);
    }
}

void page_table_init(void *ustack, void *uprogram, void *memory, size_t memorySize) {
    uart_puts("Kernel init starting...\n");

    // initialize physical page manager
    pmm_init(memorySize);

    // initialize vertual memory manager
    vmm_init();

    /* Kernel Memory */
    // map kernel memoru
    map_kernel_region();

    /* User Memory */ // TODO: implement when needed
    // setup user stack + program mapping
    // setup_initial_user_space(ustack, uprogram);

    // load page table into CPU (CR3)
    const char *err = load_page_table(get_kernel_pml4_phys());
    if (err) {
        printf("CR3 load failed: %s\n", err);
        while (1);
    }

    uart_puts("Kernel init complete.\n");

}

const char load_page_table(pte_t* pte) {
    // TODO: implement
}
