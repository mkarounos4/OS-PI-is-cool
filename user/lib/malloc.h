#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stdio.h"

#define USER_HEAP_START UINT64_C(0x400000)
#define USER_HEAP_SIZE  16384ULL

void *malloc(size_t size);
void *realloc(void *oldptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);

void *memcpy(void *dst, const void *src, size_t num);

bool mm_init(void);
void mem_init(void *start, void *end);

void *mem_sbrk(intptr_t incr);
void *mem_heap_lo();
void *mem_heap_hi();
