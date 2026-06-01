#include <stdint.h>

typedef uint64_t pte_t;

typedef struct {
    pte_t pte[512];
} page_table_t;

// TEMP static initial page table
static page_table_t l0_table __attribute__((aligned(4096)));
static page_table_t l1_table __attribute__((aligned(4096)));
static page_table_t l2_table __attribute__((aligned(4096)));
static page_table_t l3_table __attribute__((aligned(4096)));

uint64_t make_table_desc(uint64_t phys_addr);
