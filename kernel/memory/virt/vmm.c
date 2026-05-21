#include "vmm.h"

#include <stddef.h>
#include <string.h>

#include "memory/malloc.h"
#include "memory/phys/pmm.h"
#include "uart/uart.h"

#define PT_ENTRIES 512u
#define VA_INDEX_MASK UINT64_C(0x1ff)

#define PTE_VALID      (UINT64_C(1) << 0)
#define PTE_TABLE      (UINT64_C(1) << 1)
#define PTE_AF         (UINT64_C(1) << 10)
#define PTE_NG         (UINT64_C(1) << 11)
#define PTE_PXN        (UINT64_C(1) << 53)
#define PTE_UXN        (UINT64_C(1) << 54)

#define PTE_ATTR_DEVICE (UINT64_C(0) << 2)
#define PTE_ATTR_NORMAL (UINT64_C(1) << 2)
#define PTE_AP_USER     (UINT64_C(1) << 6)
#define PTE_AP_RO       (UINT64_C(1) << 7)
#define PTE_SH_INNER    (UINT64_C(3) << 8)
#define PTE_ADDR_MASK   UINT64_C(0x0000fffffffff000)

#define MAIR_ATTR_DEVICE_NGNRNE UINT64_C(0x00)
#define MAIR_ATTR_NORMAL_WB     UINT64_C(0xff)

#define TCR_T0SZ_39BIT UINT64_C(25)
#define TCR_IRGN0_WBWA (UINT64_C(1) << 8)
#define TCR_ORGN0_WBWA (UINT64_C(1) << 10)
#define TCR_SH0_INNER  (UINT64_C(3) << 12)
#define TCR_TG0_4K     (UINT64_C(0) << 14)
#define TCR_EPD1       (UINT64_C(1) << 23)
#define TCR_IPS_40BIT  (UINT64_C(2) << 32)

#define SCTLR_M (UINT64_C(1) << 0)
#define SCTLR_C (UINT64_C(1) << 2)
#define SCTLR_I (UINT64_C(1) << 12)
#define SCTLR_WXN (UINT64_C(1) << 19)
#define SCTLR_UWXN (UINT64_C(1) << 20)

/*
 * Current processes still run kernel-linked functions at EL0. Keep RAM user
 * accessible until a real user loader maps code/stacks into low VA ranges.
 */
#define VM_COMPAT_USER_IDENTITY 0

extern char __kernel_start[];
extern char __kernel_end[];

static struct address_space kernel_as;
static int vm_ready;
static int mmu_enabled;

static inline unsigned l1_index(uint64_t va) {
    return (unsigned)((va >> 30) & VA_INDEX_MASK);
}

static inline unsigned l2_index(uint64_t va) {
    return (unsigned)((va >> 21) & VA_INDEX_MASK);
}

static inline unsigned l3_index(uint64_t va) {
    return (unsigned)((va >> 12) & VA_INDEX_MASK);
}

static inline uint64_t pte_addr(pte_t entry) {
    return entry & PTE_ADDR_MASK;
}

static void tlb_invalidate_all(void) {
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory");
}

static pte_t *alloc_table(void) {
    return (pte_t *)alloc_page();
}

static pte_t make_table_desc(pte_t *table) {
    return ((uint64_t)(uintptr_t)table & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
}

static pte_t make_page_desc(uint64_t pa, uint64_t flags) {
    pte_t desc = (pa & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE | PTE_AF;

    if ((flags & VM_FLAG_DEVICE) != 0) {
        desc |= PTE_ATTR_DEVICE | PTE_PXN | PTE_UXN;
    } else {
        desc |= PTE_ATTR_NORMAL | PTE_SH_INNER;
    }

    if ((flags & VM_FLAG_USER) != 0) {
        desc |= PTE_AP_USER | PTE_NG;
    }

    if ((flags & VM_FLAG_WRITE) == 0) {
        desc |= PTE_AP_RO;
    }

    if ((flags & VM_FLAG_EXEC) == 0) {
        desc |= PTE_PXN | PTE_UXN;
    } else if ((flags & VM_FLAG_USER) == 0) {
        desc |= PTE_UXN;
    }

    return desc;
}

static pte_t *clone_table(const pte_t *src, unsigned level) {
    pte_t *dst = alloc_table();
    if (dst == NULL) {
        return NULL;
    }

    for (unsigned i = 0; i < PT_ENTRIES; i++) {
        pte_t entry = src[i];
        if ((entry & PTE_VALID) == 0) {
            continue;
        }

        if (level < 3 && (entry & PTE_TABLE) != 0) {
            pte_t *child = clone_table((const pte_t *)(uintptr_t)pte_addr(entry), level + 1);
            if (child == NULL) {
                return NULL;
            }
            dst[i] = make_table_desc(child);
        } else {
            dst[i] = entry;
        }
    }

    return dst;
}

static pte_t *get_l3_pte(struct address_space *as, uint64_t va, int create) {
    if (as == NULL || as->root_table == NULL) {
        return NULL;
    }

    pte_t *l1 = as->root_table;
    pte_t *l2;
    pte_t *l3;

    if ((l1[l1_index(va)] & PTE_VALID) == 0) {
        if (!create) {
            return NULL;
        }

        l2 = alloc_table();
        if (l2 == NULL) {
            return NULL;
        }
        l1[l1_index(va)] = make_table_desc(l2);
    }

    l2 = (pte_t *)(uintptr_t)pte_addr(l1[l1_index(va)]);

    if ((l2[l2_index(va)] & PTE_VALID) == 0) {
        if (!create) {
            return NULL;
        }

        l3 = alloc_table();
        if (l3 == NULL) {
            return NULL;
        }
        l2[l2_index(va)] = make_table_desc(l3);
    }

    l3 = (pte_t *)(uintptr_t)pte_addr(l2[l2_index(va)]);
    return &l3[l3_index(va)];
}

static void reserve_kernel_image(void) {
    uint64_t start = (uint64_t)(uintptr_t)__kernel_start;
    uint64_t end = (uint64_t)(uintptr_t)__kernel_end;
    pmm_reserve_range(start, end - start);
}

static void map_platform_devices(void) {
#if defined(PLATFORM_QEMU)
    vm_map_range(&kernel_as, UINT64_C(0x3f000000), UINT64_C(0x3f000000),
                 UINT64_C(0x01000000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
    vm_map_range(&kernel_as, UINT64_C(0x40000000), UINT64_C(0x40000000),
                 UINT64_C(0x00010000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
#elif defined(PLATFORM_RPI5)
    vm_map_range(&kernel_as, UINT64_C(0x107fff9000), UINT64_C(0x107fff9000),
                 UINT64_C(0x00020000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
    vm_map_range(&kernel_as, UINT64_C(0x1c00000000), UINT64_C(0x1c00000000),
                 UINT64_C(0x02000000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
#elif defined(PLATFORM_RPI)
    vm_map_range(&kernel_as, UINT64_C(0x107fff9000), UINT64_C(0x107fff9000),
                 UINT64_C(0x00020000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
    vm_map_range(&kernel_as, UINT64_C(0x1c00000000), UINT64_C(0x1c00000000),
                 UINT64_C(0x02000000), VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_DEVICE);
#endif
}

void vm_init(uint64_t memory_base, uint64_t memory_size) {
    pmm_init(memory_base, memory_size);
    reserve_kernel_image();

    kernel_as.root_table = alloc_table();
    kernel_as.ttbr0_phys = (uint64_t)(uintptr_t)kernel_as.root_table;

    if (kernel_as.root_table == NULL) {
        uart_puts("VM: failed to allocate kernel root table\n");
        while (1) {
            asm volatile("wfe");
        }
    }

    uint64_t ram_flags = VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_EXEC;
#if VM_COMPAT_USER_IDENTITY
    ram_flags |= VM_FLAG_USER;
#endif

    if (vm_map_range(&kernel_as, memory_base, memory_base, memory_size, ram_flags) != 0) {
        uart_puts("VM: failed to map RAM\n");
        while (1) {
            asm volatile("wfe");
        }
    }

    map_platform_devices();
    vm_ready = 1;
}

void vm_enable_kernel_mmu(void) {
    if (!vm_ready || mmu_enabled) {
        return;
    }

    uint64_t mair = MAIR_ATTR_DEVICE_NGNRNE | (MAIR_ATTR_NORMAL_WB << 8);
    uint64_t tcr = TCR_T0SZ_39BIT | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA |
                   TCR_SH0_INNER | TCR_TG0_4K | TCR_EPD1 | TCR_IPS_40BIT;
    uint64_t sctlr;

    asm volatile(
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "isb\n"
        :
        : "r"(mair), "r"(tcr), "r"(kernel_as.ttbr0_phys)
        : "memory");

    tlb_invalidate_all();

    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~(SCTLR_WXN | SCTLR_UWXN);
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
    asm volatile(
        "msr sctlr_el1, %0\n"
        "isb\n"
        :
        : "r"(sctlr)
        : "memory");

    mmu_enabled = 1;
}

int vm_is_enabled(void) {
    return mmu_enabled;
}

struct address_space *vm_kernel_address_space(void) {
    return &kernel_as;
}

struct address_space *vm_create_address_space(void) {
    if (!vm_ready) {
        return NULL;
    }

    struct address_space *as = malloc(sizeof(struct address_space));
    if (as == NULL) {
        return NULL;
    }

    as->root_table = clone_table(kernel_as.root_table, 1);
    if (as->root_table == NULL) {
        free(as);
        return NULL;
    }
    as->ttbr0_phys = (uint64_t)(uintptr_t)as->root_table;
    return as;
}

int vm_map_page(struct address_space *as, uint64_t va, uint64_t pa, uint64_t flags) {
    pte_t *pte;

    if (as == NULL || !page_aligned(va) || !page_aligned(pa)) {
        return -1;
    }

    pte = get_l3_pte(as, va, 1);
    if (pte == NULL) {
        return -1;
    }

    *pte = make_page_desc(pa, flags);
    if (mmu_enabled) {
        tlb_invalidate_all();
    }

    return 0;
}

int vm_map_range(struct address_space *as, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    uint64_t va_start = page_align_down(va);
    uint64_t pa_start = page_align_down(pa);
    uint64_t offset = va - va_start;
    uint64_t length = page_align_up(size + offset);

    for (uint64_t mapped = 0; mapped < length; mapped += PAGE_SIZE) {
        if (vm_map_page(as, va_start + mapped, pa_start + mapped, flags) != 0) {
            return -1;
        }
    }

    return 0;
}

int vm_unmap_page(struct address_space *as, uint64_t va) {
    pte_t *pte = get_l3_pte(as, va, 0);
    if (pte == NULL) {
        return -1;
    }

    *pte = 0;
    if (mmu_enabled) {
        tlb_invalidate_all();
    }

    return 0;
}

uint64_t vm_virt_to_phys(struct address_space *as, uint64_t va) {
    pte_t *pte = get_l3_pte(as, va, 0);
    if (pte == NULL || (*pte & PTE_VALID) == 0) {
        return 0;
    }

    return pte_addr(*pte) | (va & PAGE_MASK);
}

uint64_t vm_debug_get_pte(struct address_space *as, uint64_t va) {
    pte_t *pte = get_l3_pte(as, va, 0);
    if (pte == NULL) {
        return 0;
    }

    return *pte;
}

void vm_switch_address_space(struct address_space *as) {
    if (!mmu_enabled) {
        return;
    }

    if (as == NULL) {
        as = &kernel_as;
    }

    asm volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        :
        : "r"(as->ttbr0_phys)
        : "memory");
    tlb_invalidate_all();
}

int validate_user_range(struct address_space *as, uint64_t va, uint64_t len, int write) {
    if (len == 0) {
        return 0;
    }

    if (as == NULL || va + len < va) {
        return -1;
    }

    uint64_t start = page_align_down(va);
    uint64_t end = page_align_up(va + len);

    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        pte_t *pte = get_l3_pte(as, page, 0);
        if (pte == NULL || (*pte & PTE_VALID) == 0 || (*pte & PTE_AP_USER) == 0) {
            return -1;
        }

        if (write && ((*pte & PTE_AP_RO) != 0)) {
            return -1;
        }
    }

    return 0;
}

int copy_from_user(struct address_space *as, void *dst, uint64_t user_src, uint64_t len) {
    uint8_t *out = (uint8_t *)dst;

    if (validate_user_range(as, user_src, len, 0) != 0) {
        return -1;
    }

    while (len > 0) {
        uint64_t pa = vm_virt_to_phys(as, user_src);
        uint64_t page_left = PAGE_SIZE - (user_src & PAGE_MASK);
        uint64_t chunk = len < page_left ? len : page_left;

        if (pa == 0) {
            return -1;
        }

        memcpy(out, (const void *)(uintptr_t)pa, chunk);
        out += chunk;
        user_src += chunk;
        len -= chunk;
    }

    return 0;
}

int copy_to_user(struct address_space *as, uint64_t user_dst, const void *src, uint64_t len) {
    const uint8_t *in = (const uint8_t *)src;

    if (validate_user_range(as, user_dst, len, 1) != 0) {
        return -1;
    }

    while (len > 0) {
        uint64_t pa = vm_virt_to_phys(as, user_dst);
        uint64_t page_left = PAGE_SIZE - (user_dst & PAGE_MASK);
        uint64_t chunk = len < page_left ? len : page_left;

        if (pa == 0) {
            return -1;
        }

        memcpy((void *)(uintptr_t)pa, in, chunk);
        in += chunk;
        user_dst += chunk;
        len -= chunk;
    }

    return 0;
}

int copy_string_from_user(struct address_space *as, char *dst, uint64_t user_src, uint64_t max_len) {
    if (max_len == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < max_len; i++) {
        if (copy_from_user(as, &dst[i], user_src + i, 1) != 0) {
            return -1;
        }

        if (dst[i] == '\0') {
            return 0;
        }
    }

    dst[max_len - 1] = '\0';
    return -1;
}
