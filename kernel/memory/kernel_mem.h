#pragma once

#include "memory/malloc.h"

void kernel_mem_init(void *kernel_heap_start, void *kernel_heap_end);
struct mem_ctx *get_kernel_mem_ctx();
