#include "kernel_mem.h"
#include "malloc.h"

static struct mem_ctx kernel_mem_ctx;

void kernel_mem_init(void *kernel_heap_start, void *kernel_heap_end) {
    kernel_mem_ctx.heap_start = kernel_heap_start;
    kernel_mem_ctx.heap_brk = kernel_heap_start;
    kernel_mem_ctx.heap_end = kernel_heap_end;
    kernel_mem_ctx.heap_ptr = NULL;
    kernel_mem_ctx.seg_lists = NULL;

    mem_init(&kernel_mem_ctx);
}

struct mem_ctx *get_kernel_mem_ctx() {
    return &kernel_mem_ctx;
}
