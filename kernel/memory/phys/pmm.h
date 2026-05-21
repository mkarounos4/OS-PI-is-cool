#include <stdint.h>

void pmm_init(uint64_t memory_size);

void* alloc_page(void);
void* alloc_pages(size_t count);
void free_page(void* page);
