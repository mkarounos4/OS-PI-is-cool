#include <stdint.h>


void *alloc_page();

void free_page(void *page);

uint8_t pt_walk(uint64_t *l0, uint64_t va) 
