#include "malloc.h"

#include "syscall.h"

typedef struct {
    void *one_two_free_ptr;
    void *three_free_ptr;
    void *four_free_ptr;
    void *five_eight_free_ptr;
    void *nine_sixteen_free_ptr;
    void *seventeen_free_ptr;
} Seg_Lists;

static void *HeapMemoryBrk = NULL;
static void *HeapMemory = NULL;
static void *HeapMemoryEnd = NULL;
static void *heap_ptr = NULL;
static Seg_Lists *seg_lists_ptr = NULL;

static const size_t WS = 8;
static const size_t DWS = 16;
static const size_t PAGE_ALLOC_SIZE = 4096;

typedef struct free_block {
    size_t header;
    struct free_block *prev;
    struct free_block *next;
} free_block;

static void *malloc_memset(void *ptr, int value, size_t num) {
    unsigned char *p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

static size_t align(size_t x) {
    return 16 * ((x + 15) / 16);
}

void mem_init(void *start, void *end) {
    HeapMemory = start;
    HeapMemoryBrk = HeapMemory;
    HeapMemoryEnd = end;
    heap_ptr = NULL;
    seg_lists_ptr = NULL;

    if (!mm_init()) {
        while (1) {
            asm volatile("wfe");
        }
    }
}

void *mem_sbrk(intptr_t incr) {
    void *prevBrk = HeapMemoryBrk;

    if (HeapMemoryBrk == NULL || HeapMemoryEnd == NULL || incr < 0) {
        return (void *)-1;
    }

    void *nextBrk = (char *)HeapMemoryBrk + incr;
    if ((uintptr_t)nextBrk < (uintptr_t)HeapMemoryBrk ||
        (uintptr_t)nextBrk > (uintptr_t)HeapMemoryEnd) {
        return (void *)-1;
    }

    if (sbrk((uint64_t)(uintptr_t)HeapMemoryBrk,
             (uint64_t)(uintptr_t)nextBrk) < 0) {
        return (void *)-1;
    }

    HeapMemoryBrk = nextBrk;
    return prevBrk;
}

void *mem_heap_lo(void) {
    return HeapMemory;
}

void *mem_heap_hi(void) {
    return (char *)HeapMemoryBrk - 1;
}

static void insert_free(void *ptr) {
    free_block *block = (free_block *)((char *)ptr - WS);
    size_t size = *(size_t *)((char *)ptr - WS) & ~0xF;

    void **list;

    if (size <= 32) list = &seg_lists_ptr->one_two_free_ptr;
    else if (size <= 64) list = &seg_lists_ptr->three_free_ptr;
    else if (size <= 96) list = &seg_lists_ptr->four_free_ptr;
    else if (size <= 192) list = &seg_lists_ptr->five_eight_free_ptr;
    else if (size <= 512) list = &seg_lists_ptr->nine_sixteen_free_ptr;
    else list = &seg_lists_ptr->seventeen_free_ptr;

    free_block *old_head = (free_block *)*list;

    block->next = old_head;
    block->prev = NULL;

    if (old_head != NULL) old_head->prev = block;

    *list = block;
}

static void *get_free_list(size_t size) {
    free_block *fp = NULL;
    if (size <= 16 && seg_lists_ptr->one_two_free_ptr != NULL) {
        fp = (free_block *)seg_lists_ptr->one_two_free_ptr;
        while (fp != NULL) {
            if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return get_free_list(17);
    } else if (size <= 48 && seg_lists_ptr->three_free_ptr != NULL) {
        fp = (free_block *)seg_lists_ptr->three_free_ptr;
        if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
        return get_free_list(49);
    } else if (size <= 80 && seg_lists_ptr->four_free_ptr != NULL) {
        fp = (free_block *)seg_lists_ptr->four_free_ptr;
        if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
        return get_free_list(81);
    } else if (size <= 176 && seg_lists_ptr->five_eight_free_ptr != NULL) {
        fp = seg_lists_ptr->five_eight_free_ptr;
        while (fp != NULL) {
            if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return get_free_list(177);
    } else if (size <= 496 && seg_lists_ptr->nine_sixteen_free_ptr != NULL) {
        fp = seg_lists_ptr->nine_sixteen_free_ptr;
        while (fp != NULL) {
            if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return get_free_list(497);
    } else {
        fp = seg_lists_ptr->seventeen_free_ptr;
        while (fp != NULL) {
            if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return NULL;
    }
}

static void remove_free(void *ptr, size_t size) {
    free_block *block = (free_block *)((char *)ptr - WS);

    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        void **list;
        if (size <= 32) list = &seg_lists_ptr->one_two_free_ptr;
        else if (size <= 64) list = &seg_lists_ptr->three_free_ptr;
        else if (size <= 96) list = &seg_lists_ptr->four_free_ptr;
        else if (size <= 192) list = &seg_lists_ptr->five_eight_free_ptr;
        else if (size <= 512) list = &seg_lists_ptr->nine_sixteen_free_ptr;
        else list = &seg_lists_ptr->seventeen_free_ptr;

        *list = block->next;
    }

    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
}

static void *coalesce(void *ptr) {
    size_t size = *(size_t *)((char *)ptr - WS) & ~0xF;

    if ((*(size_t *)((char *)ptr - DWS) & 0x1) == 0) {
        size_t prev_size = *(size_t *)((char *)ptr - DWS) & ~0xF;

        remove_free((char *)ptr - prev_size, prev_size);

        *(size_t *)((char *)ptr - prev_size - WS) = (prev_size + size) | 0;
        *(size_t *)((char *)ptr + size - DWS) = (prev_size + size) | 0;

        size = prev_size + size;
        ptr = (char *)ptr - prev_size;
    }

    if ((*(size_t *)((char *)ptr + size - WS) & 0x1) == 0) {
        size_t next_size = *(size_t *)((char *)ptr + size - WS) & ~0xF;

        remove_free(ptr + size, next_size);

        *(size_t *)((char *)ptr - WS) = (size + next_size) | 0;
        *(size_t *)((char *)ptr + size + next_size - DWS) =
            (size + next_size) | 0;
    }

    return ptr;
}

static void initialize_free(void *ptr, size_t size) {
    *(size_t *)ptr = (size | 0);
    *(size_t *)((char *)ptr + size - WS) = (size | 0);

    ptr = coalesce((char *)ptr + WS);

    insert_free(ptr);
}

static void *extend_heap(size_t size) {
    size = align(size);

    void *old_brk = mem_sbrk(size);
    if (old_brk == (void *)-1) return NULL;

    void *header_ptr = (char *)old_brk - WS;

    *(size_t *)((char *)header_ptr + size) = (0 | 1);

    initialize_free(header_ptr, size);

    return (char *)header_ptr + WS;
}

bool mm_init(void) {
    seg_lists_ptr = mem_sbrk(sizeof(Seg_Lists));
    if (seg_lists_ptr == (void *)-1) {
        return false;
    }
    malloc_memset(seg_lists_ptr, 0, sizeof(Seg_Lists));

    heap_ptr = mem_sbrk(3 * WS);
    if (heap_ptr == (void *)-1) {
        return false;
    }

    *(size_t *)(heap_ptr) = (DWS | 1);
    *(size_t *)((char *)heap_ptr + WS) = (DWS | 1);
    *(size_t *)((char *)heap_ptr + 2 * WS) = (0 | 1);

    if (extend_heap(PAGE_ALLOC_SIZE) == NULL) {
        return false;
    }

    return true;
}

static void *get_allocation(size_t size) {
    void *ptr = get_free_list(size);
    if (ptr) {
        free_block *block = (free_block *)((char *)ptr - WS);
        size_t block_size = block->header & ~0xF;

        void **free_list;

        if (block_size <= 32) free_list = &seg_lists_ptr->one_two_free_ptr;
        else if (block_size <= 64) free_list = &seg_lists_ptr->three_free_ptr;
        else if (block_size <= 96) free_list = &seg_lists_ptr->four_free_ptr;
        else if (block_size <= 192) free_list = &seg_lists_ptr->five_eight_free_ptr;
        else if (block_size <= 512) free_list = &seg_lists_ptr->nine_sixteen_free_ptr;
        else free_list = &seg_lists_ptr->seventeen_free_ptr;

        if (block->prev != NULL) block->prev->next = block->next;
        else *free_list = block->next;

        if (block->next != NULL) block->next->prev = block->prev;

        size_t aligned_size = align(size + DWS);

        if (block_size - aligned_size >= 32) {
            block->header = aligned_size | 1;
            *(size_t *)((char *)block + aligned_size - WS) = aligned_size | 1;

            initialize_free((char *)block + aligned_size,
                            block_size - aligned_size);
        } else {
            block->header = block_size | 1;
            *(size_t *)((char *)block + block_size - WS) = block_size | 1;
        }
    }
    return ptr;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    void *ptr = get_allocation(size);

    if (ptr == NULL) {
        size_t aligned_size = align(size + DWS);
        size_t extend_size =
            (aligned_size > PAGE_ALLOC_SIZE) ? aligned_size : PAGE_ALLOC_SIZE;

        ptr = extend_heap(extend_size);
        if (ptr == NULL) return NULL;

        ptr = get_allocation(size);
        if (ptr == NULL) return NULL;
    }

    return ptr;
}

void free(void *ptr) {
    if (!ptr) return;

    void *header_ptr = (char *)ptr - WS;
    size_t size = *(size_t *)header_ptr & ~0xF;

    initialize_free(header_ptr, size);
}
