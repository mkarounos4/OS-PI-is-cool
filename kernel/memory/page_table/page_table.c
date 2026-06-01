#include "page_table.h"

/*
 * Table Descriptors (Contains physical address space of next-level table)
*/

// makes a table descriptor for a physical address
uint64_t make_table_desc(uint64_t phys_addr) {
    return (phys_addr & 0x0000FFFFFFFFF000ULL) | 11ULL;
}

void desc_table_init(void) {
    page_table_t *l0 = &l0_table;
    page_table_t *l1 = &l1_table;
    page_table_t *l2 = &l2_table;
    page_table_t *l3 = &l3_table;

    l0->pte[0] = make_table_desc((uint64_t)&l1_table);
    l1->pte[0] = make_table_desc((uint64_t)&l2_table);
    l2->pte[0] = make_table_desc((uint64_t)&l3_table);
}
