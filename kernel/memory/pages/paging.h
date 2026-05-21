#pragma once

#include <stddef.h>
#include <stdint.h>

void page_table_init(void *ustack, void *uprogram, void *memory, size_t memory_size);
