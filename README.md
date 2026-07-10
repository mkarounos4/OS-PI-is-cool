# OS-PI-is-cool

## Project summary

## Feature Overview
Below is a list of features. Note that each one links to an individual design doc with more information about said feature. You can find all of these in docs/.

Architecture/Hardware:
- Bare-metal AArch64 for Raspberry Pi 5 and QEMU emulator Raspberry Pi 3B
- EL1 kernel / EL0 userspace
- Hardware level interrupt and exception handling, system calls, timer preemption
- UART input/output
- PIO SD Card writing for proper FS persistsence

Processes:
- Multi-priority Round Robin preemptive scheduler
- fork() with Copy-On-Write
- exec() with ELF loading
- Process groups
- Signal handling with POSIX-style signal API
- Orphan and zombie handling
- Waitpid

Memory:
- Virtual memory
- Lazy stack and heap page allocation
- Demand paging for lazy allocationo of instruction memory
- Mmap
- Page-fault handling
- User/kernel and per-process isolation
- Copy-on-write fork
- Memory Allocation

Filesystem:
- ext2-style Inode based filesystem
- Directories/subdirectories
- Symlinks
- Open-file table
- Virtual Filesystem
- Character device drivers
- Least Recently Used Block Cache and Inode Cache
- Permission handling

Devices:
- UART input (w/ interrupts) and output
- TTY Terminal
- Pipes
- GUI framebuffer terminal

Shell:
- Proper shell implementation
- Userspace commands such as cat, sleep, grep, kill, ls, etc.
- Job control

## Project tree

```text
.
├── linker_rpi.ld             -- Linker.ld file for RPI5
├── linker_qemu.ld            -- Linker.ld file for QEMU RPI3B
├── user                      -- Userspace code
│   ├── linker.ld                 -- Userspace linker.ld
│   ├── user_bins.h               -- Header for user binary files
│   ├── user_boot.S               -- Assembly bootstrap for userspace ELFs
│   ├── user_linker.ld            -- Userspace linker.ld
│   ├── cmds                      -- Shell commands used to generate and load ELFs
│   └── lib                       -- Common userspace library files
├── config.txt                -- Configuration file for RPI5
├── Makefile                  -- Project Makefile
├── kernel                    -- Code compiled into the kernel image
│   ├── boot.S                    -- Kernel assembly entry point
│   ├── disk                      -- Hardware disk access files
│   ├── fan                       -- RPI5 fan-enabling code
│   ├── pipe                      -- Pipe implementation
│   ├── irq                       -- IRQ implementation
│   ├── memory                    -- Memory-management implementation
│   │   ├── mmu.c/h                     -- Virtual memory, MMU setup, and helpers
│   │   ├── mmu.S                       -- Virtual-memory assembly entry point
│   │   ├── kmalloc.c/h                 -- Kernel-level memory allocator
│   │   └── page_table                  -- Page-table implementation
│   ├── signals                   -- Signal implementation
│   ├── traps                     -- Hardware traps, interrupts, and exception logic
│   ├── uart                      -- UART, standard I/O, and hardware-address logic
│   ├── data-structs              -- Kernel data structures: lists, vectors, maps, and ring buffers
│   ├── timer                     -- Hardware and software timer implementation
│   ├── devices                   -- Filesystem device drivers and terminal implementation
│   ├── kernel.c                  -- Kernel entry point
│   ├── scheduler                 -- Scheduler and process implementation
│   ├── gui                       -- Graphical terminal GUI implementation
│   ├── syscall                   -- System-call dispatch and general helpers
│   └── fs                        -- Filesystem implementation (ext2 mock)
│       ├── types.h
│       ├── caches                  -- Cache implementations
│       │   ├── inode_cache.c/h         -- Inode-cache implementation
│       │   └── lru_cache.c/h           -- Least-recently-used block-cache implementation
│       ├── dirs.c/h                -- Directory and directory-entry implementation
│       ├── oft.c/h                 -- Open-file table implementation and helpers
│       ├── elf_loader.c/h          -- ELF-loading helpers for userspace exec
│       ├── inodes.c/h              -- Low-level inode implementation
│       ├── kapi.c/h                -- Filesystem kernel API implementations and helpers
│       ├── errors.c/h              -- Filesystem error-handling types
│       ├── disk.c/h                -- Disk wrappers around lower-level inode functions
│       └── cmds.c/h                -- High-level filesystem commands called by syscalls
└── docs                       -- Markdown documentation
```

## 
