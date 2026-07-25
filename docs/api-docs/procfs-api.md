# Procfs API Reference

`procfs` is mounted at `/proc` as a root virtual filesystem. Its files are
generated from live kernel state when opened. They can be read with normal file
APIs such as `open`, `read`, and `cat`, and inspected with `stat`.

## Root Files

| Path | Contents |
|---|---|
| `/proc/processes` | One row per live process: PID, PPID, PGID, state character, thread count, and process name. |
| `/proc/meminfo` | Managed memory totals, free/used pages, page categories, and page-fault counters. |
| `/proc/uptime` | Timer tick count and timer frequency. |
| `/proc/vmstat` | VM counters such as page faults, CoW faults/copies, mmap regions, lazy allocations, TLB flushes, page allocs, and page frees. |
| `/proc/timers` | Timer frequency, current ticks, active timer count, and active software timers with owner and wake tick. |
| `/proc/interrupts` | IRQ number, interrupt count, and IRQ name for interrupts that have fired. |
| `/proc/syscalls` | Syscall number, invocation count, and syscall name for syscalls that have been called. |
| `/proc/cache` | LRU block-cache and inode-cache capacity, usage, hit/miss, eviction, and dirty-entry counters. |
| `/proc/tty` | Active TTY count and per-TTY frontend/backend, screen, cursor, buffer, refcount, and mode state. |
| `/proc/version` | OS version string, architecture, selected platform, build timestamp, and compiler name. |
| `/proc/cpuinfo` | Processor index, architecture, current exception level, platform, page size, and timer type. |
| `/proc/threads` | One row per live thread: TID, PID, state, CPU id placeholder, and thread name. |
| `/proc/locks` | Tracked kernel locks with id, type, owner, waiter count, and lock name. |
| `/proc/mounts` | Mount table: disk root filesystem plus registered root virtual mounts such as `/proc` and `/dev`. |

## Per-Process Files

For each live process, procfs exposes `/proc/<pid>` as a virtual directory.

| Path | Contents |
|---|---|
| `/proc/<pid>/status` | Process name, ids, state, thread counters, minimum priority, exit code, cwd inode, open-file count, pending signal mask, blocked-event mask, and TTBR0. |
| `/proc/<pid>/cwd` | Current working directory inode id for the process. |
| `/proc/<pid>/fd` | User fd to kernel fd mappings for open descriptors. |
| `/proc/<pid>/maps` | User page-table segment map for the process. |
| `/proc/<pid>/threads` | Threads owned by the process with TID, state, kernel/user stack addresses, and thread name. |

## Example Output Shapes

Values vary by platform, boot state, and current workload. These examples show
the generated field shape rather than fixed expected values.

### `/proc/processes`

```text
PID PPID PGID STATE THREADS NAME
0 -1 0 R 1 init
1 0 1 R 1 shell
```

### `/proc/meminfo`

```text
MemTotal: 262144 kB
MemFree: 240000 kB
KernelHeap: 1024 kB
PageSize: 4 kB

PagesTotal: 65536
PagesFree: 60000
PagesUsed: 5536

AnonPages: 12
FilePages: 8
CowPages: 0
MappedPages: 4
KernelPages: 0
PageTables: 3

PageFaults: 20
CowFaults: 0
AnonFaults: 12
FileFaults: 8
InvalidFaults: 0
```

### `/proc/vmstat`

```text
pgfault 20
pgfault_anon 12
pgfault_file 8
pgfault_cow 0
pgfault_invalid 0
cow_copies 0
cow_shared_pages 0
mmap_regions 4
heap_lazy_allocs 2
stack_lazy_allocs 1
tlb_flushes 5
page_allocs 40
page_frees 3
```

### `/proc/threads`

```text
TID PID STATE CPU NAME
0 0 R 0 init.main
1 1 R 0 shell.main
```

### `/proc/interrupts`

```text
IRQ COUNT NAME
30 128 timer
185 4 uart
```

The UART IRQ number differs between the QEMU and Raspberry Pi 5 interrupt
paths.

### `/proc/syscalls`

```text
NR COUNT NAME
23 3 open
26 3 read
27 6 write
43 2 exec
```

### `/proc/cache`

```text
BlockCache:
  capacity_blocks: 12
  used_blocks: 4
  hits: 10
  misses: 6
  evictions: 0
  dirty_blocks: 1

InodeCache:
  capacity: 0
  used: 3
  hits: 8
  misses: 4
  evictions: 1
  dirty: 0
```

### `/proc/tty`

```text
ttys: 1
name: tty0
foreground_pgid: 1
input_backend: uart0 (1:0)
output_backend: ttygui0 (2:0)
rows: 45
cols: 120
cursor: 0,0
input_buffer: 0
output_buffer: 0
refcount: 1
canonical_mode: yes
```

## Mounts Format

`/proc/mounts` uses this header:

```text
PATH TYPE ROOT_INO
```

Current rows include `/ rootfs 1`, `/proc proc <ino>`, and `/dev dev <ino>`.
The virtual rows are generated from the VFS root mount table, so future root
virtual filesystems will appear automatically after registration.
