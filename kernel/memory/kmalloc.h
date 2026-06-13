#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void *kmalloc(size_t size);
void kfree(void *ptr);

bool kmm_init(void);
void kmem_init(void *start, void *end);

void *kmem_sbrk(intptr_t incr);
void *kmem_heap_lo();
void *kmem_heap_hi();
