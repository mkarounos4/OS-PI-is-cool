#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USER_HEAP_START UINT64_C(0x400000)
#define USER_HEAP_SIZE  16384ULL

void *malloc(size_t size);
void free(void *ptr);

bool mm_init(void);
void mem_init(void *start, void *end);

void *mem_sbrk(intptr_t incr);
void *mem_heap_lo(void);
void *mem_heap_hi(void);
