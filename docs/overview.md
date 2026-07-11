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
- Static userspace binaries
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

# Major Features

Every subsystem has a dedicated design document located in `docs/`.

## Architecture & Hardware

- Bare-metal AArch64 kernel
- Raspberry Pi 5 support
- QEMU Raspberry Pi 3B support
- EL1 kernel / EL0 userspace
- Interrupt and exception handling
- System calls
- Timer-driven preemption
- UART console
- SD card persistence

## Process Management

- Multi-priority round-robin scheduler
- `fork()` with Copy-on-Write
- `exec()` ELF loading
- Process groups
- POSIX-style signals
- Zombie and orphan handling
- `waitpid()`

## Memory Management

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

## Filesystem

- ext2-inspired inode filesystem
- Directories
- Symbolic links
- Open-file table
- Virtual filesystem layer
- Character devices
- LRU block cache
- Inode cache
- Permissions

## Devices

- UART driver
- Interrupt-driven input
- TTY terminal
- Pipes
- Framebuffer graphical terminal

## Userspace

- Interactive shell
- Job control
- ELF executable loader
- Core Unix-style commands including:

  - `cat`
  - `ls`
  - `grep`
  - `kill`
  - `sleep`
  - and many others

---

# Project Structure

```
.
├── kernel/        Kernel source
├── user/          Userspace libraries and commands
├── docs/          Design documentation
├── Makefile
├── linker_rpi.ld
├── linker_qemu.ld
└── config.txt
```

The source tree is organized by subsystem. Nearly every directory inside `kernel/` corresponds to a documented component of the operating system.

---

# Learn More

For implementation details, see the individual design documents in `docs/`, including:

- Architecture
- Scheduler
- Virtual Memory
- Filesystem
- Signals
- Pipes
- Terminal
- ELF Loader
- Userspace
- Shell
- System Calls
- Device Drivers
