#include "page_table.h"

#include "kmalloc.h"
#include "traps.h"
#include <stddef.h>
#include <stdint.h>

#include "user_image.h"

#define PAGE_MASK (PAGE_SIZE - 1ULL)
#define L1_BLOCK_SIZE UINT64_C(0x40000000)
#define PA_MASK UINT64_C(0x0000ffffffffffff)
#define BLOCK_ADDR_MASK UINT64_C(0x0000ffffc0000000)

#define DESC_BLOCK (0ULL << 1)
#define DESC_TABLE (1ULL << 1)

#define PTE_ATTRINDX(n) (((uint64_t)(n) & 0x7ULL) << 2)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_AF (1ULL << 10)
#define PTE_PXN (1ULL << 53)
#define PTE_UXN (1ULL << 54)

#define ATTR_NORMAL PTE_ATTRINDX(1)
#define ATTR_KERNEL_RX                                                         \
  (ATTR_NORMAL | PTE_AP_EL1_RO | PTE_SH_INNER | PTE_AF | PTE_UXN)
#define ATTR_KERNEL_RO (ATTR_KERNEL_RX | PTE_PXN)
#define ATTR_KERNEL_RW                                                         \
  (ATTR_NORMAL | PTE_AP_EL1_RW | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)
#define ATTR_USER_RX                                                           \
  (ATTR_NORMAL | PTE_AP_EL0_RO | PTE_SH_INNER | PTE_AF | PTE_PXN)
#define ATTR_USER_RO (ATTR_USER_RX | PTE_UXN)
#define ATTR_USER_RW                                                           \
  (ATTR_NORMAL | PTE_AP_EL0_RW | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)
#define ATTR_DEVICE                                                            \
  (PTE_ATTRINDX(0) | PTE_AP_EL1_RW | PTE_AF | PTE_PXN | PTE_UXN)

#define DEVICE_BLOCK_QEMU_LOCAL UINT64_C(0x40000000)
#define DEVICE_BLOCK_RPI_GIC UINT64_C(0x1040000000)
#define DEVICE_BLOCK_RPI5_RP1 UINT64_C(0x1c00000000)

extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __user_image_start[];
extern uint8_t __user_image_end[];
extern uint8_t __data_start[];
extern uint8_t __kernel_end[];
extern uint8_t __kernel_page_pool_start[];
extern uint8_t __RAM_end[];

struct Page *pages;

void pt_init(struct Page *page_struct_array) { pages = page_struct_array; }

typedef struct FreePage {
  struct FreePage *next;
} FreePage;

static uint64_t next_free_page;
static FreePage *free_list;

struct Page *get_page_struct(uint64_t *va) {
  uint64_t pa = kernel_phys_addr(*va);
  return &pages[pa >> 12];
}

static uint64_t align_down(uint64_t value) { return value & ~PAGE_MASK; }

static uint64_t align_up(uint64_t value) {
  return (value + PAGE_MASK) & ~PAGE_MASK;
}

static void zero_page(void *page) {
  uint64_t *words = (uint64_t *)page;
  for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
    words[i] = 0;
  }
}

static inline int64_t phys_pa_to_pfn(uint64_t phys_pa) {
    uint64_t pa = kernel_phys_addr(phys_pa);
    uint64_t pfn = pa >> 12;
    if (pages == NULL) return -1;
    return (int64_t)pfn;
}

static inline int64_t kernel_va_to_pfn(void *kernel_va) {
    if (kernel_va == NULL) return -1;
    uint64_t va = (uint64_t)(uintptr_t)kernel_va;
    uint64_t pa = kernel_phys_addr(va);
    return phys_pa_to_pfn(pa);
}

uint8_t copy_phys_page(uint64_t src_pa, uint64_t dst_pa) {
  void *src = (void *)(uintptr_t)kernel_direct_map_va(src_pa);
  void *dst = (void *)(uintptr_t)kernel_direct_map_va(dst_pa);

  uint64_t *src_words = (uint64_t *)src;
  uint64_t *dst_words = (uint64_t *)dst;

  for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
    dst_words[i] = src_words[i];
  }

  return 1;
}

static void page_allocator_init(void) {
  if (next_free_page != 0) {
    return;
  }

  next_free_page = align_up((uint64_t)(uintptr_t)__kernel_page_pool_start);
}


void *alloc_page(void) {
    page_allocator_init();

    void *page = NULL;
    if (free_list != NULL) {
        page = (void *)free_list;
        free_list = free_list->next;
    } else if (next_free_page + PAGE_SIZE <= (uint64_t)(uintptr_t)__RAM_end) {
        page = (void *)(uintptr_t)next_free_page;
        next_free_page += PAGE_SIZE;
    }

    if (page != NULL) {
        zero_page(page);

        if (pages != NULL) {
            uint64_t pa = kernel_phys_addr((uint64_t)(uintptr_t)page);
            int64_t pfn = phys_pa_to_pfn(pa);
            if (pfn >= 0) {
                pages[pfn].refcount = 1;
                pages[pfn].flags = 0;
            }
        }
    }

    return page;
}

void free_page(void *page) {
    if (page == NULL) return;

    if (pages != NULL) {
        uint64_t pa = kernel_phys_addr((uint64_t)(uintptr_t)page);
        int64_t pfn = phys_pa_to_pfn(pa);
        if (pfn >= 0) {
            pages[pfn].refcount = 0;
            pages[pfn].flags = 0;
        }
    }

    FreePage *free = (FreePage *)page;
    free->next = free_list;
    free_list = free;
}

uint64_t kernel_direct_map_va(uint64_t pa) {
  return KERNEL_VA_BASE | (pa & PA_MASK);
}

static uint64_t kernel_phys_addr(uint64_t va) { return va & PA_MASK; }

uint64_t table_desc(uint64_t *table) {
  return ((uint64_t)(uintptr_t)table & PTE_ADDR_MASK) | DESC_VALID | DESC_TABLE;
}

static uint64_t *next_table(uint64_t *table, uint64_t index) {
  if ((table[index] & DESC_VALID) == 0) {
    uint64_t *next = alloc_page();
    if (next == NULL) {
      return NULL;
    }
    table[index] = table_desc(next);
  }

  return (uint64_t *)(uintptr_t)kernel_direct_map_va(table[index] &
                                                     PTE_ADDR_MASK);
}

uint8_t pt_map_page(uint64_t *l0, uint64_t va, uint64_t pa, uint64_t attrs) {
  if (l0 == NULL || (va & PAGE_MASK) != 0 || (pa & PAGE_MASK) != 0) {
    return 0;
  }

  uint64_t l0_index = (va >> 39) & 0x1ffULL;
  uint64_t l1_index = (va >> 30) & 0x1ffULL;
  uint64_t l2_index = (va >> 21) & 0x1ffULL;
  uint64_t l3_index = (va >> 12) & 0x1ffULL;

  uint64_t *l1 = next_table(l0, l0_index);
  if (l1 == NULL) {
    return 0;
  }

  uint64_t *l2 = next_table(l1, l1_index);
  if (l2 == NULL) {
    return 0;
  }

  uint64_t *l3 = next_table(l2, l2_index);
  if (l3 == NULL) {
    return 0;
  }

  l3[l3_index] =
      (pa & PTE_ADDR_MASK) | (attrs & ~PTE_ADDR_MASK) | DESC_VALID | DESC_PAGE;
  return 1;
}

static uint8_t pt_map_l1_block(uint64_t *l0, uint64_t va, uint64_t pa,
                               uint64_t attrs) {
  if (l0 == NULL || (va & (L1_BLOCK_SIZE - 1ULL)) != 0 ||
      (pa & (L1_BLOCK_SIZE - 1ULL)) != 0) {
    return 0;
  }

  uint64_t l0_index = (va >> 39) & 0x1ffULL;
  uint64_t l1_index = (va >> 30) & 0x1ffULL;

  uint64_t *l1 = next_table(l0, l0_index);
  if (l1 == NULL) {
    return 0;
  }

  if ((l1[l1_index] & DESC_VALID) != 0) {
    return 0;
  }

  l1[l1_index] = (pa & BLOCK_ADDR_MASK) | (attrs & ~BLOCK_ADDR_MASK) |
                 DESC_VALID | DESC_BLOCK;
  return 1;
}

uint8_t pt_map_range(uint64_t *l0, uint64_t va_start, uint64_t pa_start,
                     uint64_t size, uint64_t attrs) {
  if (size == 0) {
    return 1;
  }

  uint64_t va = align_down(va_start);
  uint64_t pa = align_down(pa_start);
  uint64_t end = align_up(va_start + size);

  while (va < end) {
    if (!pt_map_page(l0, va, pa, attrs)) {
      return 0;
    }
    va += PAGE_SIZE;
    pa += PAGE_SIZE;
  }

  return 1;
}

static void *pt_map_new_page(uint64_t *l0, uint64_t va, uint64_t attrs) {
  void *page = alloc_page();
  if (page == NULL) {
    return NULL;
  }

  if (!pt_map_page(l0, align_down(va),
                   kernel_phys_addr((uint64_t)(uintptr_t)page), attrs)) {
    return NULL;
  }

  return page;
}

uint8_t pt_walk(uint64_t *l0, uint64_t va) { return pt_walk_user_page(l0, va); }

uint8_t pt_walk_user_page(uint64_t *l0, uint64_t va) {
  return pt_map_new_page(l0, va, ATTR_USER_RW) != NULL;
}

uint8_t pt_walk_kernel_page(uint64_t *l0, uint64_t va) {
  return pt_map_new_page(l0, va, ATTR_KERNEL_RW) != NULL;
}

void *pt_seed_kernel_page(uint64_t *l0, uint64_t va) {
  return pt_map_new_page(l0, va, ATTR_KERNEL_RW);
}

static uint8_t map_kernel_section(uint64_t *l0, uint8_t *va_start_ptr,
                                  uint8_t *va_end_ptr, uint64_t attrs) {
  uint64_t va_start = (uint64_t)(uintptr_t)va_start_ptr;
  uint64_t va_end = (uint64_t)(uintptr_t)va_end_ptr;
  uint64_t pa_start = kernel_phys_addr(va_start);

  return pt_map_range(l0, va_start, pa_start, va_end - va_start, attrs);
}

static uint8_t map_page_pool(uint64_t *l0) {
  uint64_t pool_start_va = (uint64_t)(uintptr_t)__kernel_page_pool_start;
  uint64_t pool_start_pa = kernel_phys_addr(pool_start_va);
  uint64_t ram_end_va = (uint64_t)(uintptr_t)__RAM_end;

  if (ram_end_va <= pool_start_va) {
    return 1;
  }

  return pt_map_range(l0, pool_start_va, pool_start_pa,
                      ram_end_va - pool_start_va, ATTR_KERNEL_RW);
}

static uint8_t map_device_block(uint64_t *l0, uint64_t pa) {
  return pt_map_l1_block(l0, kernel_direct_map_va(pa), pa, ATTR_DEVICE);
}

static uint8_t map_kernel_devices(uint64_t *l0) {
  return map_device_block(l0, DEVICE_BLOCK_QEMU_LOCAL) &&
         map_device_block(l0, DEVICE_BLOCK_RPI_GIC) &&
         map_device_block(l0, DEVICE_BLOCK_RPI5_RP1);
}

static uint8_t map_embedded_user_range(uint64_t *l0, uint64_t va_start,
                                       uint64_t va_end, uint64_t attrs) {
  if (va_end <= va_start) {
    return 1;
  }

  uint64_t va = align_down(va_start);
  uint64_t end = align_up(va_end);
  uint64_t image_start = (uint64_t)(uintptr_t)__user_image_start;

  while (va < end) {
    uint64_t image_offset = va - USER_IMAGE_START;
    uint64_t pa = kernel_phys_addr(image_start + image_offset);

    if (!pt_map_page(l0, va, pa, attrs)) {
      return 0;
    }

    va += PAGE_SIZE;
  }

  return 1;
}

static void copy_user_page(uint8_t *page, uint64_t page_va, uint64_t copy_start,
                           uint64_t copy_end, const uint8_t *source) {
  zero_page(page);

  if (source == NULL || copy_end <= copy_start) {
    return;
  }

  uint64_t start = page_va > copy_start ? page_va : copy_start;
  uint64_t end = page_va + PAGE_SIZE;
  if (end > copy_end) {
    end = copy_end;
  }

  for (uint64_t va = start; va < end; va++) {
    page[va - page_va] = source[va - USER_IMAGE_START];
  }
}

static uint8_t map_private_user_range(uint64_t *l0, uint64_t va_start,
                                      uint64_t va_end, const uint8_t *source) {
  if (va_end <= va_start) {
    return 1;
  }

  uint64_t va = align_down(va_start);
  uint64_t end = align_up(va_end);

  while (va < end) {
    uint8_t *page = alloc_page();
    if (page == NULL) {
      return 0;
    }

    copy_user_page(page, va, va_start, va_end, source);

    if (!pt_map_page(l0, va, kernel_phys_addr((uint64_t)(uintptr_t)page),
                     ATTR_USER_RW)) {
      return 0;
    }

    va += PAGE_SIZE;
  }

  return 1;
}

uint64_t *initialize_kernel_page_table(void) {
  uint64_t *l0 = alloc_page();
  if (l0 == NULL) {
    return NULL;
  }

  if (!map_kernel_section(l0, __text_start, __text_end, ATTR_KERNEL_RX)) {
    return NULL;
  }

  if (!map_kernel_section(l0, __rodata_start, __rodata_end, ATTR_KERNEL_RO)) {
    return NULL;
  }

  if (!map_kernel_section(l0, __user_image_start, __user_image_end,
                          ATTR_KERNEL_RO)) {
    return NULL;
  }

  if (!map_kernel_section(l0, __data_start, __kernel_end, ATTR_KERNEL_RW)) {
    return NULL;
  }

  if (!map_page_pool(l0)) {
    return NULL;
  }

  if (!map_kernel_devices(l0)) {
    return NULL;
  }

  return l0;
}

uint64_t *initialize_user_page_table(void) {
  uint64_t *l0 = alloc_page();
  if (l0 == NULL) {
    return NULL;
  }

  if (USER_IMAGE_END > USER_HEAP_START) {
    return NULL;
  }

  if (!map_embedded_user_range(l0, USER_TEXT_START, USER_TEXT_END,
                               ATTR_USER_RX)) {
    return NULL;
  }

  if (!map_embedded_user_range(l0, USER_RODATA_START, USER_RODATA_END,
                               ATTR_USER_RO)) {
    return NULL;
  }

  if (!map_private_user_range(l0, USER_DATA_START, USER_DATA_END,
                              __user_image_start)) {
    return NULL;
  }

  if (!map_private_user_range(l0, USER_BSS_START, USER_BSS_END, NULL)) {
    return NULL;
  }

  return l0;
}


/* COW functions and helpers */
void inc_pte_refcount_pa(uint64_t phys_pa) {
    int64_t pfn = phys_pa_to_pfn(phys_pa);
    if (pfn < 0) return;
    pages[pfn].refcount++;
}

void dec_pte_refount_pa(uint64_t phys_pa) {
    int64_t pfn = phys_pa_to_pfn(phys_pa);
    if (pfn < 0) return;
    if (pages[pfn].refcount == 0) {
        fatal_exception("[page_ref_dec_pa] refcount already zero");
    }
    pages[pfn].refcount--;
    if (pages[pfn].refcount == 0) {
        void *va = (void *)(uintptr_t)kernel_direct_map_va(phys_pa & PA_MASK);
        free_page(va);
    }
}

uint16_t get_pte_refcount_pa(uint64_t phys_pa) {
    int64_t pfn = phys_pa_to_pfn(phys_pa);
    if (pfn < 0) return 0;
    return pages[pfn].refcount;
}

void inc_pte_refcount_va(void *kernel_va) {
    int64_t pfn = kernel_va_to_pfn(kernel_va);
    if (pfn < 0) return;
    pages[pfn].refcount++;
}

void dec_pte_refcount_va(void *kernel_va) {
    int64_t pfn = kernel_va_to_pfn(kernel_va);
    if (pfn < 0) return;
    if (pages[pfn].refcount == 0) fatal_exception("[dec_pte_refcount_va] negative page refcount");
    pages[pfn].refcount--;
    if (pages[pfn].refcount == 0) {
        free_page(kernel_va);
    }
}

uint16_t get_pte_refcount_va(void *kernel_va) {
    int64_t pfn = kernel_va_to_pfn(kernel_va);
    if (pfn < 0) return 0;
    return pages[pfn].refcount;
}

int pte_is_user(uint64_t pte) {
    uint64_t ap = (pte & PTE_AP_MASK) >> PTE_AP_SHIFT;
    return (ap == (PTE_AP_EL0_RW >> PTE_AP_SHIFT)) ||
           (ap == (PTE_AP_EL0_RO >> PTE_AP_SHIFT));
}

int pte_is_writable(uint64_t pte) {
    uint64_t ap = (pte & PTE_AP_MASK) >> PTE_AP_SHIFT;
    return (ap == (PTE_AP_EL0_RW >> PTE_AP_SHIFT)) ||
           (ap == (PTE_AP_EL1_RW >> PTE_AP_SHIFT));
}


void pte_set_cow_flag(uint64_t pa, uint16_t flag) {
    int64_t pfn = phys_pa_to_pfn(pa);
    if (pfn < 0) return;
    pages[pfn].flags |= flag;
}

void pte_clear_cow_flag(uint64_t pa, uint16_t flag) {
    int64_t pfn = phys_pa_to_pfn(pa);
    if (pfn < 0) return;
    pages[pfn].flags &= ~flag;
}

uint16_t pte_test_cow_flag(uint64_t pa, uint16_t flag) {
    int64_t pfn = phys_pa_to_pfn(pa);
    if (pfn < 0) return 0;
    return (pages[pfn].flags & flag) != 0;
}


uint64_t pte_clear_writable(uint64_t pte) {
    pte &= ~PTE_AP_MASK;
    pte |= PTE_AP_EL0_RO;
    return pte;
}

void pte_make_readonly_and_mark_cow(uint64_t *pte_ptr) {
    if (pte_ptr == NULL) return;
    uint64_t pte = *pte_ptr;
    if (!pte_is_user(pte)) return;
    uint64_t pa = pte & PTE_ADDR_MASK;
    pte &= ~(3ULL << 6);
    pte |= PTE_AP_EL0_RO;
    *pte_ptr = pte;

    pte_set_cow_flag(pa, PTE_FLAG_COW);
}

void pte_clear_cow_and_make_writable(uint64_t *pte_ptr) {
    if (pte_ptr == NULL) return;
    uint64_t pte = *pte_ptr;
    uint64_t pa = pte & PTE_ADDR_MASK;
    pte_clear_cow_flag(pa, PTE_FLAG_COW);

    pte &= ~(3ULL << 6);
    pte |= PTE_AP_EL0_RW;
    *pte_ptr = pte;
}


void tlb_invalidate_all_user(void) {
    asm volatile(
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        :
        :
        : "memory");
}

