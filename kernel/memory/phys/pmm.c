#include "pmm.h"
#include <string.h>

#define MAX_MEMORY (1024ULL * 1024 * 1024) // TODO: increase for rpi5 when integrating
#define MAX_PAGES (MAX_MEMORY / PAGE_SIZE)

static uint8_t bitmap[MAX_PAGES / 8];

static uint64_t total_pages;
static uint64_t used_pages;

/* Bitmap Helpers */
static inline void bitmap_set(uint64_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int bitmap_test(uint64_t bit) {
    return bitmap[bit / 8] & (1 << (bit % 8));
}

/* PPM Functions */
void pmm_init(uint64_t memory_size) {
    total_pages = memory_size / PAGE_SIZE;

    memset(bitmap, 0, sizeof(bitmap));

    // Reserve page 0
    bitmap_set(0);

    used_pages = 1;
}


void* alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {

        if (!bitmap_test(i)) {

            bitmap_set(i);

            used_pages++;

            return (void*)(i * PAGE_SIZE);
        }
    }

    return NULL;
}

void* alloc_pages(size_t count) {
    // TODO: implement when desired
    return NULL;
}	

void free_page(void* page) {
    uint64_t index = (uint64_t)page / PAGE_SIZE;

    bitmap_clear(index);

    used_pages--;
}
