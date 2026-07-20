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

## Mounts Format

`/proc/mounts` uses this header:

```text
PATH TYPE ROOT_INO
```

Current rows include `/ rootfs 1`, `/proc proc <ino>`, and `/dev dev <ino>`.
The virtual rows are generated from the VFS root mount table, so future root
virtual filesystems will appear automatically after registration.
