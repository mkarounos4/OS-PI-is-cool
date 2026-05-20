#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mem_ctx {
    void *heap_start;
    void *heap_brk;
    void *heap_end;
    void *heap_ptr;
    void *seg_lists;
};

void *malloc(size_t size);
void free(void *ptr);

bool mm_init(void);
void mem_init(struct mem_ctx *ctx);
void mem_load_heap(struct mem_ctx *ctx);
void mem_fetch_heap_vals(struct mem_ctx *ctx);
void mem_extra_test();

void *mem_sbrk(intptr_t incr);
void *mem_heap_lo();
void *mem_heap_hi();
