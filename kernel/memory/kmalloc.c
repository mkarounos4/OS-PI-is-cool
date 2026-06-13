#include "malloc.h"

#include "uart/uart.h"

// create pointers to the heads of their segregated explicit free lists of size in double word blocks
typedef struct {
    void *one_two_free_ptr;       // size 1-2
    void *three_free_ptr;         // size 3
    void *four_free_ptr;          // size 4
    void *five_eight_free_ptr;    // size 5-8
    void *nine_sixteen_free_ptr;  // size 9-16
    void *seventeen_free_ptr;     // size 17+
} Seg_Lists;


/* Memset and Memcpy */
void *memset(void *ptr, int value, size_t num)
{
    unsigned char *p = ptr;
    while (num--)
        *p++ = (unsigned char) value;
    return ptr;
}

void *memcpy(void *dst, const void *src, size_t num)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (num--)
        *d++ = *s++;
    return dst;
}


/* Heap and Brk */
static void *HeapMemoryBrk = NULL;
static void *HeapMemory = NULL;
static void *HeapMemoryEnd = NULL;
static void *heap_ptr = NULL;
static Seg_Lists *seg_lists_ptr = NULL;

void kmem_init(void *start, void *end) {
    HeapMemory = start;
    HeapMemoryBrk = HeapMemory;
    HeapMemoryEnd = end; 
    heap_ptr = NULL;
    seg_lists_ptr = NULL;
    
    if (!kmm_init()) {
        uart_puts("ERROR: failed to initialize heap\n");
        while (1) {
            asm volatile ("wfe");
        }
    }
}

void *kmem_sbrk(intptr_t incr)
{
    void *prevBrk = HeapMemoryBrk;
    if (HeapMemoryBrk == NULL || HeapMemoryEnd == NULL || incr < 0) {
        uart_puts("ERROR: Allocated too much memory!\n");
        return (void *) -1;
    }

    void *nextBrk = (char *)HeapMemoryBrk + incr;
    if ((uintptr_t)nextBrk > (uintptr_t)HeapMemoryEnd) {
        uart_puts("ERROR: Allocated too much memory!\n");
        return (void *) -1;
    }
    HeapMemoryBrk = nextBrk;
    return prevBrk;
}

void *kmem_heap_lo()
{
    return HeapMemory;
}

void *kmem_heap_hi()
{
    return (char *)HeapMemoryBrk - 1;
}


/* Malloc */
#define ALIGNMENT 16

static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

// create word size and double word size constants
static const size_t WS = 8;
static const size_t DWS = 16;
static const size_t PAGE_SIZE = 4096;

typedef struct free_block {
    size_t header;
    struct free_block *prev;
    struct free_block *next;
} free_block;

void insert_free(void *ptr) {
    // create free block struct at ptr
    free_block *block = (free_block *)((char *)ptr - WS);
    size_t size = *(size_t *)((char *)ptr - WS) & ~0xF;

    void **list;

    if (size <= 32) list = &seg_lists_ptr->one_two_free_ptr;
    else if (size <= 64) list = &seg_lists_ptr->three_free_ptr;
    else if (size <= 96) list = &seg_lists_ptr->four_free_ptr;
    else if (size <= 192) list = &seg_lists_ptr->five_eight_free_ptr;
    else if (size <= 512) list = &seg_lists_ptr->nine_sixteen_free_ptr;
    else list = &seg_lists_ptr->seventeen_free_ptr;

    free_block *old_head = (free_block*)*list;

    block->next = old_head;
    block->prev = NULL;

    if (old_head != NULL) old_head->prev = block;

    *list = block;
}

void *get_free_list(size_t size) {
    free_block *fp = NULL; // fp = free_ptr
    if (size <= 16 && seg_lists_ptr->one_two_free_ptr != NULL) {
	fp = (free_block *)seg_lists_ptr->one_two_free_ptr;
	while(fp != NULL) {
            if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
	    fp = fp->next;
	}
	return get_free_list(17); // continue search with larger blocks
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
        while(fp != NULL) {
	    if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return get_free_list(177); // continue search with larger blocks
    } else if (size <= 496 && seg_lists_ptr->nine_sixteen_free_ptr != NULL) {
        fp = seg_lists_ptr->nine_sixteen_free_ptr;
        while(fp != NULL) {
	    if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return get_free_list(497); // continue search with larger blocks
    } else {
        fp = seg_lists_ptr->seventeen_free_ptr;
        while(fp != NULL) {
	    if ((fp->header & ~0xF) >= align(size + DWS)) return (char *)fp + WS;
            fp = fp->next;
        }
        return NULL;
    }
}
 
void remove_free(void *ptr, size_t size){
    // create free block struct at ptr
    free_block *block = (free_block *)((char *)ptr - WS);

    // if block is not head
    if (block->prev != NULL) {
	// link prev to next
	block->prev->next = block->next;
    } else {
	// determine appropriate free list
        void **list;
        if (size <= 32) list = &seg_lists_ptr->one_two_free_ptr;
        else if (size <= 64) list = &seg_lists_ptr->three_free_ptr;
        else if (size <= 96) list = &seg_lists_ptr->four_free_ptr;
        else if (size <= 192) list = &seg_lists_ptr->five_eight_free_ptr;
        else if (size <= 512) list = &seg_lists_ptr->nine_sixteen_free_ptr;
        else list = &seg_lists_ptr->seventeen_free_ptr;

	// update appropriate free list
        *list = block->next;
    }
    
    // if block is not tail
    if (block->next != NULL) {
    	block->next->prev = block->prev;
    }
}

void *coalesce(void *ptr) {
    size_t size = *(size_t *)((char *)ptr - WS) & ~0xF;
    
    // check footer of previous
    if ((*(size_t *)((char *)ptr - DWS) & 0x1) == 0) {
	// get size of previous free block
    	size_t prev_size = *(size_t *)((char *)ptr - DWS) & ~0xF;

	// remove previous from its free list
	remove_free((char *)ptr - prev_size, prev_size);

	// extend size of previous free block to include current free block
	*(size_t *)((char *)ptr - prev_size - WS) = (prev_size+ size) | 0;
        
	// extend footer size
	*(size_t *)((char *)ptr + size - DWS) = (prev_size + size) | 0;

	// update size and pointer
	size = prev_size + size;
	ptr = (char *)ptr - prev_size;
    }

    // check header of next
    if ((*(size_t *)((char *)ptr + size - WS) & 0x1) == 0) {
	
    	// get size of next free block
	size_t next_size = *(size_t *)((char *)ptr + size - WS) & ~0xF;

	// remove next from its free list
	remove_free(ptr + size, next_size);

	// extend size of current free block to include next free block
	*(size_t *)((char *)ptr - WS) = (size + next_size) | 0;

	// extend footer at end of next's size
	*(size_t *)((char *)ptr + size + next_size - DWS) = (size + next_size) | 0;
    }

    return ptr;
}

void initialize_free(void *ptr, size_t size) {
    // store required information in free block: header (8), prev (8), next (8), open space (?), footer (8)
    *(size_t *)ptr = (size | 0); // create header
    *(size_t *)((char *)ptr + size - WS) = (size | 0); // create footer

    // coalesce with any neighboring free
    ptr = coalesce((char *)ptr + WS);
    
    insert_free(ptr); // add to desired free list   
}

void *extend_heap(size_t size) {
    size = align(size); // ensure size alignment

    // extend heap
    void *old_brk = kmem_sbrk(size);
    if (old_brk == (void *)-1) return NULL;

    void *header_ptr = (char *)old_brk - WS;

    // new epilogue
    *(size_t *)((char *)header_ptr + size) = (0 | 1);
    
    initialize_free(header_ptr, size);

    return (char *)header_ptr + WS;   // return payload pointer

}

bool kmm_init(void)
{
    // allocate space for segregated lists
    seg_lists_ptr = kmem_sbrk(sizeof(Seg_Lists));
    if ( seg_lists_ptr == (void *)-1 ) {
	return false;
    }
    memset(seg_lists_ptr, 0, sizeof(Seg_Lists));

    // allocate 3 words (24 bytes) for intial prologue (16) and eqpilogue (8)
    heap_ptr = kmem_sbrk(3 * WS);
    if ( heap_ptr == (void *)-1 ) {
	   return false;
    }
	
    // allocate prologue
    *(size_t *)(heap_ptr) = (DWS | 1); // header
    *(size_t *)((char *)heap_ptr + WS) = (DWS | 1); // footer
    *(size_t *)((char *)heap_ptr + 2*WS) = (0|1); // epilogue

    if (extend_heap(PAGE_SIZE) == NULL) { // extend heap -- handles epilogue
        return false;
    }

    return true;
}

void *get_allocation(size_t size) {
    void *ptr = get_free_list(size);
    if (ptr) {
	// create free block struct at ptr
	free_block *block = (free_block *)((char *)ptr - WS);
	// determine free list
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

	    initialize_free((char*)block + aligned_size, block_size - aligned_size);

    	} else {

            block->header = block_size | 1;    
            *(size_t *)((char *)block + block_size - WS) = block_size | 1;
	}

    }
    return ptr;
}


void *kmalloc(size_t size)
{
    if (size == 0) return NULL;

    void *ptr = get_allocation(size);

    if (ptr == NULL) {
        size_t aligned_size = align(size + DWS);
        size_t extend_size = (aligned_size > PAGE_SIZE) ? aligned_size : PAGE_SIZE;

        ptr = extend_heap(extend_size);
        if (ptr == NULL) return NULL;

        // allocate newly confimed/obtained free memory
	ptr = get_allocation(size);
        if (ptr == NULL) return NULL;
    }

    return ptr;
}

void kfree(void *ptr)
{

    if (!ptr) return; // ptr is NULL -> do nothing
    
    void *header_ptr = (char *)ptr - WS;
    size_t size = *(size_t *)header_ptr & ~0xF;
    
    initialize_free(header_ptr, size);

    return;
}
