# Memory Management Overview

## List of Features

- [Virtual Memory with MMU](#mmu-virtual-memory)
- [Paging System with 4-Level Page Tables](#paging-system)
- [Kernel and User Space Isolation](#kernel-and-user-space-isolation)
- [Copy-on-Write (CoW) for efficient process forking](#copy-on-write-cow)
- [Segregated Free Lists Allocator](#segregated-free-lists-allocator)
- [Page Allocation and Management](#page-allocation-and-management)
- [TLB Management and Invalidation](#tlb-management)
- [Memory Statistics and Debugging](#memory-statistics-and-debugging)

## System Structure

```
+------------------------------------------+
|     Kernel Memory Management Layer       |
+------------------------------------------+
|      Virtual Memory / MMU Layer          |
+------------------------------------------+
|     Page Table Management Layer           |
+------------------------------------------+
|   Kernel Allocator (kmalloc/kfree)       |
+------------------------------------------+
|     Page Allocator / Free Page List      |
+------------------------------------------+
|      Physical Memory / Hardware          |
+------------------------------------------+
```

### Kernel Memory Management Layer
This is the topmost abstraction layer for memory management. It provides kernel-level allocation functions (`kmalloc`, `kfree`, `krealloc`, `kcalloc`) that utilize the page allocator and segregated free lists for efficient memory allocation. These functions are called throughout the kernel for dynamic memory allocation.

### Virtual Memory / MMU Layer
This layer handles the Memory Management Unit (MMU) initialization and exception handling for memory faults. It manages the transition from physical to virtual addressing, sets up page table registers (`TTBR0_EL1`, `TTBR1_EL1`), and handles translation faults, permission faults, and Copy-on-Write faults at the hardware level.

### Page Table Management Layer
This layer manages the hierarchical 4-level page table structure for both kernel and user address spaces. It handles page table walks, page mapping, segment loading, and maintains metadata about mapped pages including reference counts and Copy-on-Write flags.

### Kernel Allocator
This implements segregated explicit free lists for efficient allocation and deallocation within the kernel heap. It supports multiple size categories and uses coalescing to reduce fragmentation.

### Page Allocator
This manages individual 4KB pages as the fundamental unit of physical memory. It maintains a free list of available pages and provides functions for page allocation, deallocation, and copying.

### Physical Memory / Hardware
The actual RAM on the system, managed as individual pages.

---

# Detailed Architecture and Decisions

## MMU & Virtual Memory

### Overview
The Memory Management Unit (MMU) provides virtual address translation to physical addresses, enabling memory isolation between kernel and user processes, memory protection, and efficient use of physical RAM through paging.

### MMU Initialization (`initialize_mmu`)
The MMU is initialized with two separate address spaces:
- **TTBR0_EL1** (User space base register): Maps user process virtual addresses (0x10000 - 0x800000 range)
- **TTBR1_EL1** (Kernel space base register): Maps kernel virtual addresses (0xffff000000000000+ range)

Key configuration:
- **TCR_T0SZ_48BIT** / **TCR_T1SZ_48BIT**: Both address spaces use 48-bit virtual addresses (16 bits used for ASID/similar)
- **TG0_4K** / **TG1_4K**: 4KB page granule for both spaces
- **MAIR_EL1**: Memory Attribute Indirection Register configured with cacheable (normal) and device memory attributes

### Address Space Isolation

#### Kernel Address Space (TTBR1_EL1)
- **Base**: 0xffff000000000000
- **Heap**: 0xffff800000000000 (with 1MB limit)
- **Stack**: 0x900000 (per-process kernel stack of 8KB)
- Features: Direct mapping of physical RAM, device memory, and kernel data structures
- Accessed only when CPU is in EL1 (kernel mode)

#### User Address Space (TTBR0_EL1)
- **Base**: 0x10000
- **Heap**: 0x400000 (16KB limit)
- **Stack**: 0x800000 - 0x1000 (8KB, grows downward)
- Features: Contains process code, data, heap, and stack
- Accessed only when CPU is in EL0 (user mode)

### Page Table Structure

The system uses a 4-level page table hierarchy (L0, L1, L2, L3):

```
VA[48:39]  → L0 index → L1 Table
VA[38:30]  → L1 index → L2 Table or 1GB Block
VA[29:21]  → L2 index → L3 Table or 2MB Block
VA[20:12]  → L3 index → 4KB Page
VA[11:0]   → Page offset
```

Each level can contain:
- **Table Descriptors**: Point to the next level page table
- **Block Descriptors**: Direct mapping of large memory ranges (1GB at L1, 2MB at L2)
- **Page Descriptors**: Point to 4KB pages at L3 level

### Exception Handling

#### Data Abort Handler (`handle_data_abort`)
Handles various data memory faults:

1. **Translation Faults** (unmapped address):
   - For kernel heap: Lazy allocation of heap pages on demand
   - For user heap/stack: Automatic page allocation
   - For user segments: Load from ELF file via `load_segment_page_for_fault`

2. **Permission Faults** (CoW violations):
   - Check if page is Copy-on-Write enabled
   - If multiple owners (refcount > 1): Allocate new page, copy data, decrement shared refcount
   - If sole owner: Clear CoW flag and make page writable

3. **Access Flag Faults**: Handled by setting the Access Flag bit in PTE

#### Instruction Abort Handler (`handle_instruction_abort`)
Handles instruction fetch faults:
- Translation faults: Similar to data abort but for instruction addresses
- Permission faults: Verify execute permission before loading segment page
- Other faults: Fatal exceptions (address size, external aborts, etc.)

---

## Paging System

### Page Table Management (`page_table.c`)

The page table module provides the core abstraction for managing virtual-to-physical address mappings.

#### Data Structures

**Page Structure** (`page_table.h`):
```c
typedef struct Page {
    uint16_t refcount;  // Reference count for sharing (CoW)
    uint16_t flags;     // Flags including COW indicator
} Page;
```

**Page Table Structure** (internal):
```c
typedef struct page_table_st {
    uint64_t *table;    // L0 page table virtual address
    Vec segments;       // Vector of memory segments for this table
} page_table_t;
```

#### Memory Segment Tracking

Each page table tracks memory segments loaded into it for lazy loading on page faults:

**Segment Structure** (internal):
```c
typedef struct mem_segment_st {
    // Segment metadata from ELF file
    // Used to load pages on demand when page faults occur
} mem_segment_t;
```

#### Core Page Table Functions

**Mapping Functions:**
- `pt_map_page(l0, va, pa, attrs)`: Map a single 4KB page with specified attributes
- `pt_map_range(l0, va_start, pa_start, size, attrs)`: Map a contiguous range of pages
- `pt_map_l1_block(l0, va, pa, attrs)`: Map a 1GB L1 block (used during boot)

**Page Walking / Translation:**
- `pt_walk(l0, va)`: Generic page walk for current privilege level
- `pt_walk_user_page(l0, va)`: Walk user page table and allocate if needed
- `pt_walk_kernel_page(l0, va)`: Walk kernel page table

**Page Allocation:**
- `pt_seed_kernel_page(l0, va)`: Allocate and map a new kernel page at VA
- `pt_seed_user_page(l0, va)`: Allocate and map a new user page at VA
- `pt_get_mapped_page(l0, va)`: Get kernel virtual address of a mapped page

**Page Table Initialization:**
- `initialize_kernel_page_table()`: Create and populate kernel page table
- `initialize_user_page_table()`: Create empty user page table (populated on demand)

**Page Table Metadata:**
- `add_page_table_struct(table)`: Register a new page table for tracking
- `free_page_table_struct(table)`: Unregister and cleanup page table tracking
- `destroy_page_table(table)`: Destroy entire page table hierarchy
- `copy_page_table_struct(src, dst)`: Copy page table structure for fork()

#### Segment Loading

**Function: `load_memory_segment`**
Loads a segment from disk (ELF file) into memory:
- Parameters: inode ID, file offset, file size, virtual address, physical address, memory size, ELF flags
- Lazy loads pages when page faults occur (see `load_segment_page_for_fault`)

**Function: `load_segment_page_for_fault`**
Called during page fault to load a specific page from an ELF segment:
- Performs permission checks (execute/read/write from ELF flags)
- Reads page data from disk inode
- Maps page into page table
- Returns status: PAGE_FAULT_HANDLED, PAGE_FAULT_PERMISSION, or PAGE_FAULT_ERROR

### Page Attributes & Protection

**Permission Attributes** (ARMv8):
- `PTE_AP_EL1_RW`: Kernel read-write access
- `PTE_AP_EL1_RO`: Kernel read-only access
- `PTE_AP_EL0_RW`: User read-write access
- `PTE_AP_EL0_RO`: User read-only access

**Predefined Attribute Macros:**
- `ATTR_KERNEL_RX`: Kernel read-execute (not writable, user cannot execute)
- `ATTR_KERNEL_RO`: Kernel read-only (execute disabled)
- `ATTR_KERNEL_RW`: Kernel read-write (execute disabled)
- `ATTR_USER_RX`: User read-execute
- `ATTR_USER_RO`: User read-only
- `ATTR_USER_RW`: User read-write
- `ATTR_DEVICE`: Device memory (no caching, execute disabled)

### Device Memory Mapping

Specific physical memory ranges are mapped as device memory for hardware peripherals:

```c
DEVICE_BLOCK_QEMU_LOCAL      0x40000000      // QEMU local peripherals
DEVICE_BLOCK_RPI5_PCIE       0x1000000000    // RPi5 PCIe
DEVICE_BLOCK_RPI_GIC         0x1040000000    // RPi GIC interrupt controller
DEVICE_BLOCK_RPI5_RP1_PERIPH 0x1c00000000    // RPi5 RP1 peripherals
DEVICE_BLOCK_RPI5_RP1_MSIX   0x1f80000000    // RPi5 RP1 MSI-X
```

---

## Copy-on-Write (CoW)

### Overview
Copy-on-Write is an optimization for `fork()` that defers physical page copying until one process attempts to write to a shared page. This saves memory and makes forking very efficient.

### CoW State Tracking

**Reference Counting:**
- `inc_pte_refcount_pa(pa)`: Increment reference count for page at physical address
- `dec_pte_refcount_pa(pa)`: Decrement reference count
- `get_pte_refcount_pa(pa)`: Read current reference count
- `inc_pte_refcount_va(va)`: Same functions using kernel virtual address
- `dec_pte_refcount_va(va)`
- `get_pte_refcount_va(va)`

**CoW Flag Management:**
- `pte_set_cow_flag(pa, flag)`: Set CoW flag on page
- `pte_clear_cow_flag(pa, flag)`: Clear CoW flag
- `pte_test_cow_flag(pa, flag)`: Test if CoW flag is set
- `PTE_FLAG_COW` (0x1): Bit flag indicating page is Copy-on-Write

### CoW Write Fault Handling

When a write permission fault occurs on a user page:

1. **Check CoW Status**: Verify `PTE_FLAG_COW` is set on the page
2. **Check Refcount**:
   - **Refcount > 1** (shared page):
     - Allocate new physical page
     - Copy data from shared page to new page
     - Update PTE to point to new page
     - Decrement refcount of old shared page
     - Clear CoW flag and make page writable
   - **Refcount = 1** (sole owner):
     - Simply clear CoW flag and make page writable
3. **TLB Invalidation**: Flush TLB to ensure CPU uses updated mapping

### CoW in Fork Operations

During `copy_page_table_struct` (fork):
- Iterate through all mapped user pages in parent
- Increment reference count for each page
- Make page read-only on both parent and child
- Set CoW flag on both page table entries
- Child process gets new page table pointing to same physical pages
- On next write to either process: CoW fault handling kicks in

---

## Segregated Free Lists Allocator

### Overview
The kernel heap uses segregated explicit free lists for efficient allocation and deallocation. The allocator organizes free blocks into size categories to reduce fragmentation and search time.

### Allocator Design (`kmalloc.c`)

#### Free List Categories
Six segregated free lists based on block size (in double words, 16-byte units):

| List | Size Range | Purpose |
|------|-----------|---------|
| `one_two_free_ptr` | 1-2 DW (16-32 bytes) | Small allocations |
| `three_free_ptr` | 3 DW (48 bytes) | Medium-small |
| `four_free_ptr` | 4 DW (64 bytes) | Medium |
| `five_eight_free_ptr` | 5-8 DW (80-128 bytes) | Medium-large |
| `nine_sixteen_free_ptr` | 9-16 DW (144-256 bytes) | Large |
| `seventeen_free_ptr` | 17+ DW (272+ bytes) | Very large |

#### Block Metadata Structure

Each free block contains explicit pointers for doubly-linked list management:
```c
typedef struct free_block {
    size_t header;              // Size and allocated bit
    struct free_block *prev;    // Previous free block
    struct free_block *next;    // Next free block
} free_block;
```

Allocated blocks use:
- **Header (8 bytes before payload)**: Contains size and allocated bit (LSB)
- **Footer (8 bytes after payload)**: Mirrors header for quick boundary detection

#### Core Operations

**Allocation (`kmalloc`)**:
1. Request size is aligned to 16-byte boundary
2. Search appropriate segregated free list
3. If found: remove from list, mark as allocated, split if excess
4. If not found: extend heap with `sbrk`, retry allocation

**Deallocation (`kfree`)**:
1. Retrieve header to find block size
2. Mark as free (clear allocated bit)
3. Coalesce with adjacent free blocks (if any)
4. Insert into appropriate segregated list

**Coalescing (`coalesce`)**:
- **Backward coalesce**: Check if previous block is free (via footer)
  - If free: merge with previous, update sizes
- **Forward coalesce**: Check if next block is free (via header)
  - If free: merge with current, update sizes
- Significantly reduces fragmentation

**Heap Extension (`extend_heap`)**:
- Uses `kmem_sbrk` to request additional memory from page allocator
- Default page size: 4KB
- New memory initialized as single free block

#### Alignment and Constants

- **ALIGNMENT**: 16 bytes (double word size)
- **WS** (Word Size): 8 bytes
- **DWS** (Double Word Size): 16 bytes
- **PAGE_SIZE**: 4KB (aligned heap extensions)

#### Performance Characteristics

- **Allocation**: O(list_size) in worst case, typically O(1) for well-sized allocations
- **Deallocation**: O(1) with doubly-linked lists
- **Coalescing**: O(1) due to immediate coalescing on free
- **Fragmentation**: Minimized by segregation and immediate coalescing

### Initialization (`kmm_init`)

1. Allocate space for segregated list pointers (48 bytes)
2. Initialize prologue (allocated sentinel block)
3. Extend heap with 4KB initial allocation
4. All lists start as empty

### Memory Layout

```
[Prologue: 16B] [Epilogue: 8B] | [Free blocks...] [Epilogue: 8B]
                                  ↑ Heap start
```

---

## Page Allocation and Management

### Page Allocator (`page_table.c`)

The page allocator manages individual 4KB physical pages as the atomic unit of memory allocation.

#### Physical Memory Tracking

**Page Structure Array**:
- Initialized at boot with physical memory range
- One `Page` structure per 4KB of memory
- Stores reference count and flags for each page

**Free Page List**:
```c
typedef struct FreePage {
    struct FreePage *next;
} FreePage;
```
- Intrusive linked list embedded in free pages themselves
- Head pointer maintained in `free_list_head`

#### Core Functions

**`alloc_page()`**:
- Pop page from free list
- Zero the page contents
- Return kernel virtual address (direct-mapped)
- Increment page metadata refcount

**`free_page(void *page)`**:
- Push page back to free list head
- Decrement refcount
- Page becomes available for reallocation

**`copy_phys_page(src_pa, dst_pa)`**:
- Copy 4KB from source physical address to destination
- Used during CoW fault handling
- Verifies both addresses are valid

#### Direct Mapping

Kernel pages are accessed via direct mapping:
- Physical address = Virtual address & PA_MASK (0xffffffffffff)
- Kernel virtual addresses in 0xffff000000000000+ range
- Eliminates need for virtual-to-physical lookups in kernel code

#### Page Statistics

Functions for monitoring memory usage:

- `page_table_count_free_list_pages()`: Count available pages
- `page_table_total_managed_pages()`: Total pages under management
- `page_table_free_managed_pages()`: Free pages in managed pool
- `page_table_count_cow_shared_pages()`: Pages with refcount > 1
- `page_table_count_mmap_regions()`: Tracked mapped regions

---

## TLB Management

### Translation Lookaside Buffer

The TLB is a hardware cache of recent virtual-to-physical translations, speeding up address translation.

#### TLB Invalidation Functions

**`tlb_invalidate_all_user()`**:
```asm
dsb ishst        // Data synchronization barrier
tlbi vmalle1is   // Invalidate all stage-1 TLB entries
dsb ish          // Inner shareable domain barrier
isb              // Instruction synchronization barrier
```
- Flushes all user (EL0) address space translations
- Used after CoW page replacements

**When Invalidation is Needed**:
1. **CoW Write Fault Resolved**: Page replaced or made writable
2. **Page Table Updates**: Any change to mapped pages
3. **Process Context Switch**: (if implemented)
4. **During Exception Handling**: After page allocation/mapping

#### Memory Barriers

- **DSB (Data Synchronization Barrier)**: Ensures memory operations complete before continuing
- **ISB (Instruction Synchronization Barrier)**: Flushes pipeline for consistency
- **Inner Shareable Domain**: Synchronization across CPU cores

---

## Memory Statistics and Debugging

### Statistics Formatting

#### `/proc/meminfo` Format (`page_table_format_meminfo`)

Provides system-wide memory statistics:
```
MemTotal:         XXXX kB  // Total managed memory
MemFree:          XXXX kB  // Free pages available
MemAllocated:     XXXX kB  // Allocated pages
CoWShared:        XXXX kB  // Pages shared via CoW
```

#### `/proc/vmstat` Format (`page_table_format_vmstat`)

Tracks memory-related events:
```
page_faults               X  // Total page faults
anon_faults               X  // Anonymous memory faults
anon_heap_faults          X  // Heap segment faults
anon_stack_faults         X  // Stack segment faults
invalid_faults            X  // Invalid memory accesses
cow_faults                X  // CoW write faults
cow_copies                X  // Pages copied for CoW
tlb_flushes               X  // TLB invalidations
```

#### Segment Information (`page_table_format_segments`)

Per-process loaded segments:
```
Segment: [name] [va_start-va_end] [permissions]
```

### Event Tracking

Functions for recording memory events for statistics:

- `page_table_note_anon_fault(heap_fault, stack_fault)`: Record anonymous page allocation
- `page_table_note_invalid_fault()`: Record invalid memory access attempt
- `page_table_note_cow_fault()`: Record CoW write fault
- `page_table_note_cow_copy()`: Record page copy during CoW
- `page_table_note_tlb_flush()`: Record TLB invalidation

---

## Kernel and User Space Isolation

### Memory Layout and Isolation

#### Kernel Space (0xffff000000000000 - 0xffffffffffffffff)

```
0xffff000000000000 - 0xffff7fffffffffff  →  Kernel mapped memory
0xffff800000000000 - 0xffff8000000fffff  →  Kernel heap (1MB)
0xffff900000000000  →  Per-process kernel stack (8KB)
```

- Mapped via TTBR1_EL1
- Only accessible in EL1 (kernel mode)
- Contains kernel code, data, heaps, and stacks
- Device memory mappings

#### User Space (0x10000 - 0x9fffff)

```
0x10000 - 0x3fffff           →  Process code/text
0x400000 - 0x400000+16KB     →  Process heap
0x800000 - (0x800000-8KB)    →  Process stack (8KB, downward)
```

- Mapped via TTBR0_EL1
- Only accessible in EL0 (user mode)
- Each process has isolated copy via separate page tables
- Contains process segments loaded from ELF files

### Hardware-Enforced Protection

The MMU hardware enforces:

1. **Privilege Level Checks**: Based on PTE AP bits
   - `PTE_AP_EL1_*`: Only kernel can access
   - `PTE_AP_EL0_*`: Both kernel and user can access

2. **Permission Checks**:
   - Read permission (`PTE_AP_EL*_RO/RW`)
   - Write permission (`PTE_AP_EL*_RW`)
   - Execute permission (`PTE_UXN`, `PTE_PXN`)

3. **Access Control**:
   - Violations trap to kernel exception handlers
   - Kernel decides: allow (fix), deny (signal), or fatal error

---

## Boot-time Memory Setup

### Early MMU Initialization (`initialize_mmu`)

During boot before kernel main:

1. **Configure MAIR_EL1**: Set up memory attribute encodings
   - Normal cacheable memory
   - Device memory (non-cached)

2. **Configure TCR_EL1**: Set up table control
   - 48-bit address spaces for both kernel and user
   - 4KB page granule
   - Cache policies

3. **Load TTBR0_EL1/TTBR1_EL1**: Point to boot page tables
4. **Enable MMU**: Set SCTLR_EL1.M bit to turn on translation
5. **Jump to kernel**: Continue execution with virtual addressing

### Boot Page Tables

Pre-allocated boot page tables mapped at compile-time:
- `boot_ttbr0_l0/l1`: User space mappings
- `boot_ttbr1_l0/l1`: Kernel space mappings
- Used until runtime page tables fully initialized

### Runtime Page Table Installation

**`install_kernel_page_table()`**:
- Calls `initialize_kernel_page_table()` to create final kernel page table
- Loads into TTBR1_EL1
- Invalidates TLB
- From this point, runtime kernel page table is used

---

## Summary of Key Design Decisions

1. **4-Level Hierarchical Page Tables**: Efficient 48-bit address space translation with large page support
2. **Separate TTBR0/TTBR1**: Clean kernel/user isolation without ASID complexity
3. **Lazy Page Allocation**: Kernel heap pages allocated on demand via page faults
4. **Segregated Free Lists**: Reduces fragmentation and allocation time vs monolithic heap
5. **Copy-on-Write**: Efficient process forking with shared pages until modified
6. **Direct Mapping**: Kernel pages directly mapped eliminates translation overhead for kernel code
7. **Exception-Driven Faults**: Uses MMU exceptions (translation faults, permission faults) to drive allocation and protection
8. **Hardware-Enforced Protection**: ARMv8 PTE bits enforce all memory permissions and access controls
