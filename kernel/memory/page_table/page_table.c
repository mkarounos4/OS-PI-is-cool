#include "page_table.h"

#include "kmalloc.h"
#include "traps.h"
#include <stddef.h>
#include <stdint.h>
#include "data-structs/hashmap.h"
#include "data-structs/vec.h"
#include "fs/disk.h"
#include "fs/errors.h"
#include "uart.h"

#include "user_image.h"

#define PAGE_MASK (PAGE_SIZE - 1ULL)
#define L1_BLOCK_SIZE UINT64_C(0x40000000)
#define PA_MASK UINT64_C(0x0000ffffffffffff)
#define BLOCK_ADDR_MASK UINT64_C(0x0000ffffc0000000)

#define DESC_BLOCK (0ULL << 1)
#define DESC_TABLE (1ULL << 1)

#define ELF_PF_X 0x1
#define ELF_PF_W 0x2
#define ELF_PF_R 0x4

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
#define DEVICE_BLOCK_RPI5_PCIE UINT64_C(0x1000000000)
#define DEVICE_BLOCK_RPI_GIC UINT64_C(0x1040000000)
#define DEVICE_BLOCK_RPI5_RP1_PERIPH UINT64_C(0x1c00000000)
#define DEVICE_BLOCK_RPI5_RP1_MSIX UINT64_C(0x1f80000000)

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

typedef struct mem_segment_st {
  ino_id_t ino_id;
  uint64_t file_offset;
  uint64_t file_size;
  uint64_t va;
  uint64_t pa;
  uint64_t mem_size;
  uint32_t flags;
} mem_segment_t;

typedef struct page_table_st {
  uint64_t *table;
  Vec segments;
} page_table_t;

static HashMap page_table_structs;

static void mem_segment_destroy(ptr_t value) {
  kfree(value);
}

static void page_table_struct_destroy(hashmap_value_t value) {
  page_table_t *page_table = (page_table_t *)value;
  if (page_table == NULL) {
    return;
  }

  vec_destroy(&page_table->segments);
  kfree(page_table);
}

static void page_table_structs_init(void) {
  if (page_table_structs.bucket_count != 0) {
    return;
  }

  page_table_structs = hashmap_new(16, page_table_struct_destroy);
}

static page_table_t *get_page_table_struct(uint64_t *table) {
  page_table_structs_init();

  uint64_t table_key = (uint64_t)(uintptr_t)table;
  hashmap_value_t value = NULL;
  if (!hashmap_get(&page_table_structs,
                   HASHMAP_KEY_FROM_UINT64(table_key), &value)) {
    return NULL;
  }

  return (page_table_t *)value;
}

int add_page_table_struct(uint64_t *table) {
  if (table == NULL) {
    return 0;
  }

  page_table_structs_init();

  if (get_page_table_struct(table) != NULL) {
    return 1;
  }

  page_table_t *page_table = kmalloc(sizeof(page_table_t));
  if (page_table == NULL) {
    return 0;
  }

  page_table->table = table;
  page_table->segments = vec_new(2, mem_segment_destroy);

  uint64_t table_key = (uint64_t)(uintptr_t)table;
  if (!hashmap_put(&page_table_structs,
                   HASHMAP_KEY_FROM_UINT64(table_key), page_table)) {
    page_table_struct_destroy(page_table);
    return 0;
  }

  return 1;
}

void free_page_table_struct(uint64_t *table) {
  if (table == NULL || page_table_structs.bucket_count == 0) {
    return;
  }

  uint64_t table_key = (uint64_t)(uintptr_t)table;
  hashmap_remove(&page_table_structs, HASHMAP_KEY_FROM_UINT64(table_key),
                 NULL);
}

static void destroy_page_table_level(uint64_t *table, int level) {
  if (table == NULL) {
    return;
  }

  for (size_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    uint64_t desc = table[i];
    if ((desc & DESC_VALID) == 0) {
      continue;
    }

    uint64_t pa = desc & PTE_ADDR_MASK;
    if (level == 3) {
      dec_pte_refcount_pa(pa);
    } else if ((desc & DESC_TABLE) == DESC_TABLE) {
      uint64_t *next =
          (uint64_t *)(uintptr_t)kernel_direct_map_va(pa);
      destroy_page_table_level(next, level + 1);
    }
  }

  free_page(table);
}

void destroy_page_table(uint64_t *table) {
  if (table == NULL) {
    return;
  }

  free_page_table_struct(table);
  destroy_page_table_level(table, 0);
}

int copy_page_table_struct(uint64_t *src_table, uint64_t *dst_table) {
  page_table_t *src = get_page_table_struct(src_table);
  if (src == NULL) {
    return INVALID_ARGS;
  }

  free_page_table_struct(dst_table);

  if (!add_page_table_struct(dst_table)) {
    return INVALID_ARGS;
  }

  page_table_t *dst = get_page_table_struct(dst_table);
  if (dst == NULL) {
    return INVALID_ARGS;
  }

  for (size_t i = 0; i < vec_len(&src->segments); i++) {
    mem_segment_t *src_segment = vec_get(&src->segments, i);
    mem_segment_t *dst_segment = kmalloc(sizeof(mem_segment_t));
    if (dst_segment == NULL) {
      return INVALID_ARGS;
    }

    *dst_segment = *src_segment;
    vec_push_back(&dst->segments, dst_segment);
  }

  return SUCCESS;
}

static uint64_t user_segment_attrs(uint32_t flags) {
  if ((flags & ELF_PF_W) != 0) {
    return ATTR_USER_RW;
  }

  if ((flags & ELF_PF_X) != 0) {
    return ATTR_USER_RX;
  }

  return ATTR_USER_RO;
}

static int segment_allows_fault(mem_segment_t *segment, int instruction_fault) {
  if (instruction_fault) {
    return (segment->flags & ELF_PF_X) != 0;
  }

  return (segment->flags & (ELF_PF_R | ELF_PF_W)) != 0;
}

static void copy_bytes(void *dst, const void *src, size_t size) {
  uint8_t *dst_bytes = dst;
  const uint8_t *src_bytes = src;
  for (size_t i = 0; i < size; i++) {
    dst_bytes[i] = src_bytes[i];
  }
}

static uint64_t min_u64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static uint64_t max_u64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

static int read_segment_page(mem_segment_t *segment, uint64_t page_va,
                             void *page) {
  uint64_t copy_start = max_u64(page_va, segment->va);
  uint64_t copy_end = min_u64(page_va + PAGE_SIZE,
                              segment->va + segment->mem_size);
  if (copy_end <= copy_start) {
    return INVALID_ARGS;
  }

  uint64_t file_copy_end = min_u64(copy_end, segment->va + segment->file_size);
  if (file_copy_end <= copy_start) {
    return SUCCESS;
  }

  uint64_t segment_file_offset = segment->file_offset + copy_start - segment->va;
  int inode_file_size = get_file_size_by_id(segment->ino_id);
  if (inode_file_size < 0) {
    return inode_file_size;
  }
  if (segment_file_offset >= (uint64_t)inode_file_size) {
    return SUCCESS;
  }

  uint64_t bytes_to_read =
      min_u64(file_copy_end - copy_start,
              (uint64_t)inode_file_size - segment_file_offset);
  uint64_t page_offset = copy_start - page_va;
  int block_size = get_bytes_per_block();
  uint8_t *block_data = kmalloc((size_t)block_size);
  if (block_data == NULL) {
    return INVALID_ARGS;
  }

  while (bytes_to_read > 0) {
    unsigned int file_block_index = segment_file_offset / (uint64_t)block_size;
    uint64_t block_offset = segment_file_offset % (uint64_t)block_size;
    block_no_t block_no =
        get_ith_block_of_file_by_id(segment->ino_id, file_block_index);
    if (block_no == 0) {
      kfree(block_data);
      return FILE_READ_ERROR;
    }

    err_t err = read_block(block_data, block_no);
    if (err != SUCCESS) {
      kfree(block_data);
      return err;
    }

    uint64_t bytes_from_block =
        min_u64(bytes_to_read, (uint64_t)block_size - block_offset);
    copy_bytes((uint8_t *)page + page_offset, block_data + block_offset,
               (size_t)bytes_from_block);

    page_offset += bytes_from_block;
    segment_file_offset += bytes_from_block;
    bytes_to_read -= bytes_from_block;
  }

  kfree(block_data);
  return SUCCESS;
}

int load_segment_page_for_fault(uint64_t *table, uint64_t fault_va,
                                int instruction_fault) {
  page_table_t *page_table = get_page_table_struct(table);
  if (page_table == NULL) {
    return PAGE_FAULT_NOT_HANDLED;
  }

  for (size_t i = 0; i < vec_len(&page_table->segments); i++) {
    mem_segment_t *segment = vec_get(&page_table->segments, i);
    if (fault_va < segment->va ||
        fault_va >= segment->va + segment->mem_size) {
      continue;
    }

    if (!segment_allows_fault(segment, instruction_fault)) {
      return PAGE_FAULT_PERMISSION;
    }

    uint64_t page_va = fault_va & ~PAGE_MASK;
    void *page = alloc_page();
    if (page == NULL) {
      return PAGE_FAULT_ERROR;
    }

    int err = read_segment_page(segment, page_va, page);
    if (err != SUCCESS) {
      free_page(page);
      return PAGE_FAULT_ERROR;
    }

    uint64_t pa = kernel_phys_addr((uint64_t)(uintptr_t)page);
    if (!pt_map_page(table, page_va, pa, user_segment_attrs(segment->flags))) {
      free_page(page);
      return PAGE_FAULT_ERROR;
    }

    return PAGE_FAULT_HANDLED;
  }

  return PAGE_FAULT_NOT_HANDLED;
}

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

uint64_t kernel_phys_addr(uint64_t va) { return va & PA_MASK; }

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

void *pt_seed_user_page(uint64_t *l0, uint64_t va) {
  return pt_map_new_page(l0, va, ATTR_USER_RW);
}

void *pt_get_mapped_page(uint64_t *l0, uint64_t va) {
  if (l0 == NULL) {
    return NULL;
  }

  uint64_t l0_index = (va >> 39) & 0x1ffULL;
  uint64_t l1_index = (va >> 30) & 0x1ffULL;
  uint64_t l2_index = (va >> 21) & 0x1ffULL;
  uint64_t l3_index = (va >> 12) & 0x1ffULL;

  if ((l0[l0_index] & DESC_VALID) == 0) {
    return NULL;
  }

  uint64_t *l1 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l0[l0_index] &
                                                             PTE_ADDR_MASK);
  if ((l1[l1_index] & DESC_VALID) == 0) {
    return NULL;
  }

  uint64_t *l2 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l1[l1_index] &
                                                             PTE_ADDR_MASK);
  if ((l2[l2_index] & DESC_VALID) == 0) {
    return NULL;
  }

  uint64_t *l3 = (uint64_t *)(uintptr_t)kernel_direct_map_va(l2[l2_index] &
                                                             PTE_ADDR_MASK);
  if ((l3[l3_index] & DESC_VALID) == 0) {
    return NULL;
  }

  uint64_t pa = (l3[l3_index] & PTE_ADDR_MASK) | (va & PAGE_MASK);
  return (void *)(uintptr_t)kernel_direct_map_va(pa);
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
         map_device_block(l0, DEVICE_BLOCK_RPI5_PCIE) &&
         map_device_block(l0, DEVICE_BLOCK_RPI_GIC) &&
         map_device_block(l0, DEVICE_BLOCK_RPI5_RP1_PERIPH) &&
         map_device_block(l0, DEVICE_BLOCK_RPI5_RP1_MSIX);
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

  if (!add_page_table_struct(l0)) {
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

void dec_pte_refcount_pa(uint64_t phys_pa) {
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

int load_memory_segment(uint64_t *table, ino_id_t ino_id,
                        uint64_t file_offset, uint64_t file_size,
                        uint64_t va, uint64_t pa, uint64_t mem_size,
                        uint32_t flags) {
  page_table_t *page_table = get_page_table_struct(table);
  if (page_table == NULL) {
    if (!add_page_table_struct(table)) {
      return INVALID_ARGS;
    }
    page_table = get_page_table_struct(table);
    if (page_table == NULL) {
      return INVALID_ARGS;
    }
  }

  mem_segment_t *segment = kmalloc(sizeof(mem_segment_t));
  if (segment == NULL) {
    return INVALID_ARGS;
  }

  segment->ino_id = ino_id;
  segment->file_offset = file_offset;
  segment->file_size = file_size;
  segment->va = va;
  segment->pa = pa;
  segment->mem_size = mem_size;
  segment->flags = flags;

  vec_push_back(&page_table->segments, segment);

  return SUCCESS;
}
