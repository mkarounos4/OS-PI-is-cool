# Table of Contents

- [Overview](#overview)
- [Project Goals](#project-goals)
- [Design Philosophy](#design-philosophy)
- [What Makes It Unix-like](#what-makes-it-unix-like)
- [What Is Intentionally Simplified](#what-is-intentionally-simplified)
- [Kernel Space vs User Space](#kernel-space-vs-user-space)
- [Why Raspberry Pi 5 + QEMU Pi 3B](#why-raspberry-pi-5--qemu-pi-3b)
- [Major Accomplishments](#major-accomplishments)
- [Current Feature Status](#current-feature-status)
- [Future Enhancements](#future-enhancements)
- [Major Features](#major-features)
- [Learn More](#learn-more)
- [Project tree](#project-tree)

## Other Documentation Files

| Document | Scope |
|---|---|
| [Quickstart Guide](docs/quickstart.md) | Build, rebuild, Raspberry Pi 5 boot, and QEMU boot instructions. |
| [Demo Guide](docs/demo.md) | Demo workflow and commands to show the OS running. |
| [Architecture](docs/architecture/architecture.md) | Boot flow, linker layout, platform split, EL1/EL0 boundary, IRQs, timers, traps, and syscalls. |
| [Filesystem Architecture](docs/architecture/filesystem.md) | Inode filesystem, VFS, open-file table, caches, permissions, and disk layout. |
| [Processes Architecture](docs/architecture/processes.md) | Scheduler, fork, exec, process groups, zombies/orphans, waitpid, multithreading, synchronization, isolation. |
| [Userspace Architecture](docs/architecture/userspace.md) | Userspace build pipeline, linker scripts, embedded ELF blobs, EL0 isolation, init, shell, and user libraries. |
| [Memory Architecture](docs/architecture/memory.md) | Virtual Memory, per-process page tables, lazy allocation, demand paging, page fault handling, copy-on-write, malloc |
| [Device Drivers Architecture](docs/architecture/device-drivers.md) | Block devices, SDHCI, UART, char devices, TTY backends, framebuffer terminal, pipes, fan, and driver init order. |
| [Syscall API Reference](docs/api-docs/syscall-table.md) | Raw syscall table with SVC numbers and brief syscall notes. |
| [Userspace API Reference](docs/api-docs/user-api.md) | Userspace library functions, shell helpers, and command mini man pages. |
| [Procfs API Reference](docs/api-docs/procfs-api.md) | `/proc` files, generated fields, and mount-table reporting. |
| [Signals API Reference](docs/api-docs/signals-api.md) | Signals ids, default dispositions, usage. |

# Overview

This project is a Unix-inspired operating system written from scratch for **AArch64**. It runs both on **Raspberry Pi 5 hardware** and under **QEMU's Raspberry Pi 3B emulator**, providing a complete educational operating system with virtual memory, multitasking, a persistent filesystem, and a userspace environment.

Rather than reproducing every feature of a modern Unix kernel, the project focuses on implementing the core concepts that make Unix systems elegant and understandable, with each subsystem documented in detail throughout `docs/`.

---

# Project Goals

The primary goals of this operating system are:

- Build a complete Unix-inspired operating system from scratch
- Develop every major kernel subsystem without relying on an existing OS
- Emphasize clean architecture and readable code
- Demonstrate modern operating-system concepts through practical implementation
- Provide comprehensive documentation for every major subsystem

The result is an educational operating system that implements many of the mechanisms found in traditional Unix kernels while remaining approachable enough to understand as a complete codebase.

---

# Design Philosophy

Several principles guide the design of the project.

- **Keep the architecture modular.** Each subsystem has well-defined responsibilities.
- **Follow Unix ideas where practical.** Processes, files, permissions, pipes, and signals all follow familiar Unix semantics.
- **Prefer correctness over optimization.** Clarity and maintainability take precedence over micro-optimizations.
- **Document every subsystem.** Every major component links to a dedicated design document describing both implementation and rationale.
- **Develop incrementally.** Features are built one subsystem at a time rather than all at once.

---

# What Makes It Unix-like

The operating system adopts many of the classic Unix abstractions.

- Process-based execution model
- `fork()` / `exec()` process creation
- POSIX-style signal handling
- Hierarchical inode-based filesystem
- User and kernel privilege separation
- Virtual memory with process isolation
- Pipes for interprocess communication
- Permissions and ownership
- Shell with job control
- Small userspace utilities

While not POSIX-complete, the system intentionally mirrors familiar Unix behavior whenever practical.

---

# What Is Intentionally Simplified

Some areas are intentionally reduced in scope to keep the codebase understandable.

Examples include:

- Single-core execution
- No networking stack
- Minimal driver framework
- Limited device support
- Simplified ext2-inspired filesystem instead of a production filesystem
- Educational implementations over highly optimized production algorithms

These tradeoffs allow the project to focus on the operating-system fundamentals rather than production-scale complexity.

---

# Kernel Space vs User Space

The operating system follows a traditional split between privileged kernel code and isolated user processes.

## Kernel Space

Kernel responsibilities include:

- Scheduler
- Virtual memory manager
- Process management
- Interrupt and exception handling
- System call dispatcher
- Filesystem
- Device drivers
- Pipes
- Signal delivery
- Terminal drivers
- ELF loading

## User Space

User space contains:

- Shell
- Core command-line utilities
- User libraries
- ELF executables
- Test programs

```
+----------------------------+
|       User Programs        |
|  shell • ls • cat • grep   |
+----------------------------+
|      System Call API       |
+----------------------------+
|          Kernel            |
| Scheduler • VM • FS • IPC  |
| Drivers • Signals • TTY    |
+----------------------------+
| Raspberry Pi Hardware      |
+----------------------------+
```

---

# Why Raspberry Pi 5 + QEMU Pi 3B

Development targets two complementary platforms.

## Raspberry Pi 5

The Raspberry Pi 5 provides modern ARM64 hardware for running the operating system on real hardware with full peripheral support.

## QEMU Raspberry Pi 3B

QEMU enables rapid development, debugging, and automated testing without requiring physical hardware.

Supporting both platforms makes development significantly faster while ensuring the kernel also runs correctly on real hardware.

---

# Major Accomplishments

Major completed subsystems include:

- Full virtual memory implementation
- Copy-on-write `fork()`
- ELF executable loading
- Preemptive multitasking
- Process groups and job control
- POSIX-style signals
- Persistent inode-based filesystem
- Virtual filesystem layer
- Demand paging
- Lazy page allocation
- Graphical framebuffer terminal
- Interactive shell with userspace commands

---

# Current Feature Status

| Area | Implemented |
|------|-------------|
| Processes | `fork`, `exec`, `waitpid`, `exit`, process groups |
| Virtual Memory | Page tables, Copy-on-Write, lazy allocation, page faults, `mmap` |
| Filesystem | Inode filesystem, directories, symbolic links, permissions, VFS |
| IPC | Pipes, signals |
| Terminal | UART TTY, graphical framebuffer terminal |
| Userspace | Statically linked ELF executables and shell commands |

---

## Future Enhancements

The following is a list of Future Enhancements we are in progress of making.

- TCP Networking Stack
- Custom Search Engine
- Multicore Support
- Proper GUI Desktop Environment
- Custom Package Manager
- On-device C compiler to proogram our OS and make system calls with C
- More userspace functions and POSIX API calls

---

# Major Features

Every subsystem has a dedicated design document located in `docs/`.

## [Architecture & Hardware](docs/architecture/architecture.md)

- Bare-metal AArch64 kernel
- Raspberry Pi 5 support
- QEMU Raspberry Pi 3B support
- EL1 kernel / EL0 userspace
- Interrupt and exception handling
- System calls
- Timer-driven preemption
- UART console
- SD card persistence

## [Process Management](docs/architecture/processes.md)

- Multi-priority round-robin scheduler
- `fork()` with Copy-on-Write
- `exec()` ELF loading
- Process groups
- POSIX-style signals
- Zombie and orphan handling
- `waitpid()`
- Multithreading and Synchronization

## [Memory Management](docs/architecture/memory.md)

- Virtual memory
- Page tables
- Lazy stack allocation
- Lazy heap allocation
- Demand paging
- Page fault handling
- Process isolation
- Copy-on-Write
- Kernel memory allocator
- `mmap()`

## [Filesystem](docs/architecture/filesystem.md)

- ext2-inspired inode filesystem
- Directories
- Symbolic links
- Open-file table
- Virtual filesystem layer
- `procfs` and `devfs` root virtual filesystems
- Character devices
- LRU block cache
- Inode cache
- Permissions

## [Devices](docs/architecture/device-drivers.md)

- UART driver
- Interrupt-driven input
- TTY terminal, with multi-terminal support
- Raw vs Canonical terminal mode
- Pipes
- Framebuffer graphical terminal

## [Userspace](docs/architecture/userspace.md)

- Interactive shell
- Job control
- ELF executable loader
- Core Unix-style commands including:

  - `cat`
  - `ls`
  - `grep`
  - `kill`
  - `sleep`
  - `vim` style text editor
  - `pong`
  - and many others

---

## Project tree

```text
.
├── Makefile                       -- Cross-build, userspace ELF, QEMU, and install targets
├── README.md                      -- Project overview and repository map
├── build_to_sd                    -- Helper script for SD-card deployment
├── config.txt                     -- Raspberry Pi boot configuration
├── linker.ld                      -- Kernel linker script for supported platforms
├── user
│   ├── user_boot.S                -- EL0 userspace entry bootstrap
│   ├── user_bins.h                -- Embedded userspace binary table interface
│   ├── user_linker.ld             -- Userspace ELF linker script
│   ├── linker.ld                  -- Alternate userspace linker script
│   ├── lib                        -- Userspace syscall wrappers and libc-style helpers
│   │   ├── errno.c/h              -- Errno names, messages, and printing
│   │   ├── fs_syscall.h           -- Filesystem syscall wrappers and constants
│   │   ├── malloc.c/h             -- Userspace heap allocator and memory helpers
│   │   ├── signals.h              -- Signal wrappers, constants, and sigaction types
│   │   ├── stdio.c/h              -- printf and puts
│   │   ├── string.c/h             -- Minimal string and parsing helpers
│   │   ├── syscall.c/h            -- Base syscall wrappers and process helpers
│   │   ├── tests.c/h              -- Userspace smoke tests
│   │   └── tty_syscall.h          -- TTY and alternate-screen wrappers
│   └── cmds                       -- Statically linked userspace commands
│       ├── shell.c/h              -- Interactive shell
│       ├── shell                  -- Shell parser, jobs, vectors, and I/O helpers
│       └── *.c                    -- cat, chmod, clear, cp, echo, grep, ls, vim, wc, etc.
├── kernel
│   ├── boot.S                     -- Kernel assembly entry point
│   ├── kernel.c                   -- Kernel C entry point
│   ├── errno.h                    -- Kernel errno values
│   ├── string.c/h                 -- Kernel string helpers
│   ├── data-structs               -- Hash map, linked list, ring buffer, and vector
│   ├── devices                    -- Device registry and TTY devices
│   ├── disk                       -- Block-device and SDHCI support
│   ├── fan                        -- Raspberry Pi 5 fan support
│   ├── fs                         -- Filesystem, VFS, procfs, ELF loader, and file table
│   │   ├── caches                 -- Inode cache and LRU block cache
│   │   ├── cmds.c/h               -- Filesystem commands called by syscalls
│   │   ├── dirs.c/h               -- Directory operations
│   │   ├── disk.c/h               -- Filesystem disk layout and mount support
│   │   ├── elf_loader.c/h         -- Userspace ELF loading
│   │   ├── errors.c/h             -- Filesystem error handling
│   │   ├── inodes.c/h             -- Inode operations
│   │   ├── kapi.c/h               -- File-descriptor kernel API
│   │   ├── oft.c/h                -- Open-file table
│   │   ├── devfs.c/h              -- Devfs virtual device nodes
│   │   ├── procfs.c/h             -- Procfs virtual files
│   │   ├── types.h                -- Filesystem types
│   │   └── virtual_fs.c/h         -- Virtual filesystem routing
│   ├── gui                        -- Framebuffer GUI and terminal rendering
│   │   └── tty_gui_device.c/h     -- Registered framebuffer TTY backend char driver
│   ├── irq                        -- Interrupt controller logic
│   ├── memory                     -- MMU, kmalloc, user allocator, and page tables
│   │   └── page_table             -- Page-table construction and lookup helpers
│   ├── pipe                       -- Pipe implementation
│   ├── scheduler                  -- Process, thread scheduling, and context switch
│   ├── signals                    -- Kernel signal delivery
│   ├── syscall                    -- Syscall dispatcher and /proc syscall formatting
│   ├── threading                  -- Threads, mutexes, semaphores, and condition variables
│   ├── timer                      -- Timer ticks, sleeps, and delays
│   ├── traps                      -- Exception vectors and trap handling
│   └── uart                       -- UART drivers and kernel printf
│       └── uart_device.c/h        -- Registered UART backend char driver
└── docs
    ├── quickstart.md              -- Build and boot instructions
    ├── demo.md                    -- Demo workflow notes
    ├── architecture               -- Subsystem architecture documents
    │   ├── architecture.md        -- Hardware, boot, linker layout, traps, IRQs, timers, and syscalls
    │   ├── device-drivers.md      -- Block devices, char drivers, UART, TTY, TTYGUI, pipes, and fan
    │   ├── filesystem.md          -- Inodes, VFS, mkfs, mount, caches, permissions, and dev nodes
    │   ├── memory.md              -- MMU, page tables, page faults, COW, and allocators
    │   ├── processes.md           -- Process architecture placeholder
    │   └── userspace.md           -- Userspace build, linker scripts, ELF embedding, init, shell, and libs
    └── api-docs
        ├── procfs-api.md          -- Procfs file reference and output fields
        ├── signals-api.md         -- Signal ids, defaults, masks, sigaction, and signal helpers
        ├── syscall-table.md       -- Raw syscall/SVC reference
        └── user-api.md            -- Userspace library and command reference
```
