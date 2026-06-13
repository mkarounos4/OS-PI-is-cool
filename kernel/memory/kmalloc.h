#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void free(void *ptr);

bool mm_init(void);
void mem_init(void *start, void *end);

void *mem_sbrk(intptr_t incr);
void *mem_heap_lo();
void *mem_heap_hi();
