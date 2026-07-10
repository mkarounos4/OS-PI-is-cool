
#include "lib/malloc.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uintptr_t heap_start = (uintptr_t)mem_heap_lo();
    uintptr_t heap_hi = (uintptr_t)mem_heap_hi();
    unsigned int used = 0;
    if (mem_heap_lo() != 0 && mem_heap_hi() != 0 && heap_hi >= heap_start) {
        used = (unsigned int)(heap_hi - heap_start + 1);
    }

    printf("heap total=%u used=%u free=%u\n",
           (unsigned int)USER_HEAP_SIZE,
           used,
           (unsigned int)USER_HEAP_SIZE - used);
    return 0;
}
