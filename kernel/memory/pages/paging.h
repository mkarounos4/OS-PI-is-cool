#include "kernel.c"
#include "uart/uart.h"
#include "memory/malloc.h"
#include <stdint.h>
#include <string.h>

extern const size_t PAGE_SIZE = 4096;
static const size_t PTE_COUNT = MAX_MEMORY / PAGE_SIZE;
static const size_t PDE_COUNT = PTE_COUNT / 512; // PTE_COUNT / 512

// TODO: Verify if necessary
// static const uint64_t VA_START = 0xFFFFFFFFFFE00000;

// TODO: confirm params
void page_table_init(void *ustack, void *uprogram, void *memory, size_t memorySize);
