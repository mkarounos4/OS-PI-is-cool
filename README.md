# OS-PI-is-cool

A bare-metal AArch64 Unix-style operating system for Raspberry Pi 5 hardware and QEMU, with user/kernel isolation, virtual memory, copy-on-write `fork`, ELF userspace, an inode VFS, POSIX-style signals, threads, memory-mapped device I/O, and graphical terminals.

## Table of Contents

- [Overview](#overview)
- [Current Feature Status](#current-feature-status)
- [Why This Was Hard](#why-this-was-hard)
- [Demo](#demo)
- [Project Scope](#project-scope)
- [Project Goals](#project-goals)
- [Design Philosophy](#design-philosophy)
- [What Makes It Unix-style](#what-makes-it-unix-style)
- [What Is Intentionally Simplified](#what-is-intentionally-simplified)
- [Other Documentation Files](#other-documentation-files)
- [Kernel Space vs User Space](#kernel-space-vs-user-space)
- [Why Raspberry Pi 5 + QEMU Pi 3B](#why-raspberry-pi-5--qemu-pi-3b)
- [Major Accomplishments](#major-accomplishments)
- [Future Enhancements](#future-enhancements)
- [Major Features](#major-features)
- [Project tree](#project-tree)

---

## Overview

OS-PI-is-cool is a Unix-inspired operating system written from scratch for **AArch64**. It runs on **Raspberry Pi 5 hardware** and under **QEMU's Raspberry Pi 3B emulator**, combining bare-metal hardware bring-up with realistic OS mechanisms: virtual memory, multitasking, persistent storage, a filesystem-backed `/bin`, and an interactive userspace shell.

The project is intentionally educational rather than a complete POSIX implementation. It focuses on the core mechanics that make Unix-style systems understandable: processes own resources, threads are scheduled, files and devices share descriptor paths, page faults drive memory behavior, and userspace reaches the kernel through a narrow syscall ABI.

---

## Current Feature Status

| Area | Implemented |
|---|---|
| Architecture | Bare-metal AArch64 kernel, EL1/EL0 isolation, traps, IRQs, syscalls, timer preemption |
| Processes | `fork`, `exec`, `waitpid`, `exit`, process groups, zombies, orphans, job control |
| Scheduling | Multi-priority round-robin scheduling, timer interrupts, blocking sleep, thread wakeups |
| Virtual Memory | Per-process page tables, page faults, lazy allocation, demand paging, `mmap`, copy-on-write |
| ELF Loading | Runtime `exec`, ELF validation, argument stack setup, lazy `PT_LOAD` paging from `/bin` |
| Filesystem | Inode filesystem, directories, symlinks, permissions, VFS, file descriptors, block/inode caches |
| Virtual Filesystems | `procfs`, `devfs`, device nodes, mount-table reporting |
| IPC | Pipes, POSIX-style signals, process-group signal delivery, `SIGCHLD` |
| Threads | Kernel/user threading support, mutexes, semaphores, condition variables |
| Drivers / Devices | Memory-mapped hardware drivers, UART, SD/block device support, framebuffer, TTY backends, fan |
| Terminal / GUI | UART TTY, framebuffer graphical terminal, multi-terminal support, raw/canonical mode |
| Userspace | ELF executables, shell, user libraries, Unix-style commands, tests, and editor utilities |

---

## Why This Was Hard

This project goes beyond a toy kernel or emulator-only OS. Raspberry Pi 5 low-level documentation is limited, so hardware support required cross-referencing available documentation, Linux source, device-tree behavior, ARMv8-A documentation, and observed MMIO behavior to initialize and drive real devices.

The OS also implements Unix-style semantics across interacting subsystems rather than isolated features. `fork`, copy-on-write memory, page faults, ELF loading, file descriptors, pipes, signals, process groups, job control, and `waitpid` all interact through the scheduler, trap return path, filesystem, and virtual memory system.

The result is a full vertical stack: boot code, kernel, drivers, memory manager, filesystem, syscall layer, userspace runtime, shell, and applications.

---

## Demo

A full demo guide is available in [docs/demo.md](docs/demo.md). The demo is intended to show:

- Booting the kernel on QEMU or Raspberry Pi 5
- Entering userspace and launching the shell
- Running ELF userspace commands from `/bin`
- Creating, reading, and persisting files
- Demonstrating `fork`, `exec`, `waitpid`, pipes, and signals
- Inspecting process and kernel state through `/proc`
- Showing UART and framebuffer-backed terminal output

Screenshots and video clips can be added here as the public demo page is finalized.

---

## Project Scope

This repository contains the kernel, userspace runtime, shell commands, architecture documentation, and API references for OS-PI-is-cool. The project is designed to be understandable as a complete operating-system codebase while still implementing realistic Unix-style mechanisms such as process isolation, filesystem-backed execution, signals, pipes, and persistent storage.

---

## Project Goals

The primary goals of this operating system are:

- Build a complete Unix-inspired operating system from scratch
- Develop every major kernel subsystem without relying on an existing OS
- Emphasize clean architecture and readable code
- Demonstrate modern operating-system concepts through practical implementation
- Provide comprehensive documentation for every major subsystem

The result is an educational operating system that implements many of the mechanisms found in traditional Unix kernels while remaining approachable enough to understand as a complete codebase.

---

## Design Philosophy

Several principles guide the design of the project.

- **Keep the architecture modular.** Each subsystem has well-defined responsibilities.
- **Follow Unix ideas where practical.** Processes, files, permissions, pipes, and signals all follow familiar Unix semantics.
- **Prefer correctness over optimization.** Clarity and maintainability take precedence over micro-optimizations.
- **Document every subsystem.** Every major component links to a dedicated design document describing both implementation and rationale.
- **Develop incrementally.** Features are built one subsystem at a time rather than all at once.

---

## What Makes It Unix-style

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

While not a complete POSIX implementation, the system intentionally mirrors familiar Unix behavior whenever practical.

---

## What Is Intentionally Simplified

The OS is designed as an educational Unix-style kernel rather than a production replacement for Linux. Some production-scale features are intentionally out of scope:

- Single-core execution instead of SMP
- No networking stack yet
- Focused hardware support for the devices needed to boot, interact with, render output, and persist data
- ext2-inspired educational filesystem rather than a fully POSIX-compliant production filesystem
- Simple, readable subsystem designs over highly optimized production algorithms

These tradeoffs keep the full OS understandable while still implementing the core mechanisms of a Unix-style kernel.

---

## Other Documentation Files

### General Docs

| Document | Scope |
|---|---|
| [Quickstart Guide](docs/quickstart.md) | Build, rebuild, Raspberry Pi 5 boot, and QEMU boot instructions. |
| [Demo Guide](docs/demo.md) | Demo workflow and commands to show the OS running. |
| [Testing and Validation](docs/testing.md) | Smoke tests, manual validation flows, and debugging interfaces. |

### Architecture Docs

| Document | Scope |
|---|---|
| [Architecture](docs/architecture/architecture.md) | Boot flow, linker layout, platform split, EL1/EL0 boundary, IRQs, timers, traps, and syscalls. |
| [Filesystem Architecture](docs/architecture/filesystem.md) | Inode filesystem, VFS, open-file table, caches, permissions, and disk layout. |
| [Processes Architecture](docs/architecture/processes.md) | Scheduler, trap-frame return path, context switching, fork, exec, process groups, zombies/orphans, waitpid, sleep blocking, multithreading, synchronization, isolation. |
| [ELF Loading Architecture](docs/architecture/elf-loading.md) | Runtime `exec`, ELF validation, argument stack setup, page-table replacement, lazy segment loading, and demand paging from `/bin`. |
| [Signals Architecture](docs/architecture/signals.md) | Kernel signal delivery, masks, pending sets, default actions, process groups, job-control signals, SIGCHLD, and scheduler delivery checkpoints. |
| [Userspace Architecture](docs/architecture/userspace.md) | Userspace build pipeline, linker scripts, embedded ELF blobs, EL0 isolation, init, shell, and user libraries. |
| [Memory Architecture](docs/architecture/memory.md) | Virtual memory, per-process page tables, lazy allocation, demand paging, page fault handling, copy-on-write, and allocators. |
| [Device Drivers Architecture](docs/architecture/device-drivers.md) | Block devices, SDHCI, UART, char devices, TTY backends, framebuffer terminal, pipes, fan, and driver init order. |

### API Docs

| Document | Scope |
|---|---|
| [Syscall API Reference](docs/api-docs/syscall-table.md) | Raw syscall table with SVC numbers and brief syscall notes. |
| [Userspace API Reference](docs/api-docs/user-api.md) | Userspace library functions, shell helpers, and command mini man pages. |
| [Procfs API Reference](docs/api-docs/procfs-api.md) | `/proc` files, generated fields, and mount-table reporting. |
| [Signals API Reference](docs/api-docs/signals-api.md) | Signal ids, default dispositions, masks, `sigaction`, and signal helper behavior. |

---

## Kernel Space vs User Space

The operating system follows a traditional split between privileged kernel code and isolated user processes.

### Kernel Space

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

### User Space

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

## Why Raspberry Pi 5 + QEMU Pi 3B

Development targets two complementary platforms.

### Raspberry Pi 5

The Raspberry Pi 5 provides modern ARM64 hardware for running the operating system on real hardware with the supported UART, framebuffer, interrupt, fan, and SD-backed storage paths.

### QEMU Raspberry Pi 3B

QEMU enables rapid development, debugging, and automated testing without requiring physical hardware.

Supporting both platforms makes development significantly faster while ensuring the kernel also runs correctly on real hardware.

---

## Major Accomplishments

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

## Future Enhancements

Planned or in-progress areas:

- TCP/IP networking stack
- Multicore/SMP support
- More complete POSIX userspace APIs
- GUI desktop environment on top of the framebuffer terminal system
- On-device C toolchain or small C-like compiler for writing userspace programs inside the OS
- Package-management or search tooling for discovering and installing userspace programs

---

## Major Features

Every subsystem has a dedicated design document located in `docs/`.

### [Architecture & Hardware](docs/architecture/architecture.md)

- Bare-metal AArch64 kernel
- Raspberry Pi 5 support
- QEMU Raspberry Pi 3B support
- EL1 kernel / EL0 userspace
- Interrupt and exception handling
- System calls
- Timer-driven preemption
- Software timers and `sleep()`
- Error handling and fatal exception policy
- UART console
- SD card persistence

### [Process Management](docs/architecture/processes.md)

- Multi-priority round-robin scheduler
- End-to-end trap frame, scheduler interrupt, context switch, and EL0 return path
- `fork()` with Copy-on-Write
- `exec()` process replacement
- Process groups
- Zombie and orphan handling
- `waitpid()`
- Timer-backed sleep blocking
- Multithreading and Synchronization

### [ELF Loading](docs/architecture/elf-loading.md)

- Filesystem-backed `exec()`
- AArch64 ELF validation
- `argc`/`argv` stack construction
- Fresh TTBR0 page-table replacement
- Lazy `PT_LOAD` segment registration
- Demand paging executable pages from `/bin`
- Init and shell command integration

### [Signals](docs/architecture/signals.md)

- POSIX-style signal actions and masks
- Process-level and thread-level pending signals
- Process-group signal delivery
- `SIGCHLD` and `waitpid()` wakeups
- TTY job-control signals
- Scheduler checkpoint delivery

### [Memory Management](docs/architecture/memory.md)

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

### [Filesystem](docs/architecture/filesystem.md)

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

### [Devices](docs/architecture/device-drivers.md)

- Memory-mapped hardware driver implementation
- Block-device and SDHCI support for persistent storage
- UART driver and interrupt-driven input
- Character-device layer for TTY backends
- UART TTY and framebuffer graphical TTY
- Multi-terminal support with raw and canonical modes
- Raspberry Pi 5 fan/device support
- Device initialization order and kernel driver registration

### [Userspace](docs/architecture/userspace.md)

- Interactive shell
- Job control
- Userspace ELF build and `/bin` seeding
- Core Unix-style commands including:

  - `cat`
  - `ls`
  - `grep`
  - `kill`
  - `sleep`
  - `vim` style text editor
  - `wc`
  - and other small utilities

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
    ├── testing.md                 -- Smoke tests, manual validation, and debugging interfaces
    ├── architecture               -- Subsystem architecture documents
    │   ├── architecture.md        -- Hardware, boot, linker layout, traps, IRQs, timers, and syscalls
    │   ├── device-drivers.md      -- Block devices, char drivers, UART, TTY, TTYGUI, pipes, and fan
    │   ├── elf-loading.md         -- Runtime exec, ELF validation, stack setup, and lazy segment paging
    │   ├── filesystem.md          -- Inodes, VFS, mkfs, mount, caches, permissions, and dev nodes
    │   ├── memory.md              -- MMU, page tables, page faults, COW, and allocators
    │   ├── processes.md           -- Scheduler, context switching, fork, exec, waitpid, sleep, and resources
    │   ├── signals.md             -- Signal delivery, masks, process groups, job control, and SIGCHLD
    │   └── userspace.md           -- Userspace build, linker scripts, ELF embedding, init, shell, and libs
    └── api-docs
        ├── procfs-api.md          -- Procfs file reference and output fields
        ├── signals-api.md         -- Signal ids, defaults, masks, sigaction, and signal helpers
        ├── syscall-table.md       -- Raw syscall/SVC reference
        └── user-api.md            -- Userspace library and command reference
```
