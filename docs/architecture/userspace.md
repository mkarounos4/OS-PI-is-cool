# Userspace Architecture

This document describes how userspace is built, packaged into the kernel image,
isolated from kernel space, entered by the scheduler, and connected to kernel
services. It does not repeat the userspace API reference; individual library
functions and command mini man pages are documented in
`docs/api-docs/user-api.md`.

## List of Features

- [Userspace model](#userspace-model)
- [Build pipeline](#build-pipeline)
- [Userspace linker scripts](#userspace-linker-scripts)
- [Userspace startup code](#userspace-startup-code)
- [Embedding user programs in the kernel image](#embedding-user-programs-in-the-kernel-image)
- [Seeding `/bin`](#seeding-bin)
- [EL0 isolation](#el0-isolation)
- [Process address space](#process-address-space)
- [Syscall boundary](#syscall-boundary)
- [Init process](#init-process)
- [Shell and command execution](#shell-and-command-execution)
- [Userspace heap](#userspace-heap)
- [User libraries](#user-libraries)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)

## System Structure

```
+--------------------------------------------------+
|              user/cmds/*.c programs              |
+--------------------------------------------------+
|       user/lib wrappers, stdio, malloc, string    |
+--------------------------------------------------+
|       user_boot.S _start and user linker script   |
+--------------------------------------------------+
|         command ELF files in build/.../bin        |
+--------------------------------------------------+
|    generated .incbin objects embedded in kernel   |
+--------------------------------------------------+
|     mkfs/mount seed /bin, scheduler execs init    |
+--------------------------------------------------+
|      EL0 execution through TTBR0 user mappings    |
+--------------------------------------------------+
```

# Detailed Architecture and Decisions

## Userspace Model

Userspace programs are ordinary freestanding AArch64 ELF binaries built from
the `user/` tree. They run at EL0, use their own process page tables, and enter
the kernel only through `svc #0`.

The userspace tree is split into:

- `user/cmds`: executable programs such as `init`, `shell`, `cat`, `ls`, `vim`,
  `grep`, and `ps`.
- `user/cmds/shell`: shell parser, job table, and shell helpers.
- `user/lib`: shared userspace support code for syscalls, stdio, strings,
  malloc, errno, signals, tests, and TTY helpers.
- `user/user_boot.S`: the `_start` stub linked into each userspace program.
- `user/user_linker.ld`: the current linker script for command ELFs.
- `user/linker.ld`: an older/direct-entry linker script that still documents
  the same user virtual layout with `init_process_entry` symbols.
- `user/user_bins.h`: kernel-visible metadata type for embedded command blobs.

The architectural boundary is simple: userspace owns program logic and libc-like
helpers; the kernel owns memory mappings, files, devices, scheduling, signals,
and privileged CPU state.

## Build Pipeline

The Makefile builds userspace before linking the final kernel image.

The build discovers:

- every top-level `user/cmds/*.c` file as a command.
- every `user/lib/*.c` file as shared userspace library code.
- every `user/cmds/shell/*.c` file as extra objects for the shell only.

Each command is compiled with freestanding flags:

- `-ffreestanding`
- `-nostdlib`
- `-mgeneral-regs-only`
- `-fno-pic`
- `-fno-pie`
- `-fno-stack-protector`
- unwind table generation disabled

Those flags keep the binaries independent of a host libc, dynamic loader, PIC
runtime, stack protector runtime, or exception unwinding support. The OS only
provides the small userspace library in `user/lib`.

Each command is linked into:

```
build/<platform>/user/bin/<command>.elf
```

The normal link rule combines:

- `user_boot.S.o`
- the command object
- all `user/lib` objects
- `user/user_linker.ld`

The shell has a special rule because it also links parser, job-control, vector,
and I/O helper objects from `user/cmds/shell`.

The output command files are real ELF binaries. They are not linked into the
kernel as executable kernel code. They are embedded as byte blobs so the kernel
can seed them into the filesystem under `/bin`.

## Userspace Linker Scripts

The current command linker script is `user/user_linker.ld`. It defines:

- `ENTRY(_start)`
- `USER_VA_BASE = 0x10000`
- loadable `text`, `rodata`, and `data` program headers
- page-aligned text, rodata, data, and BSS boundaries
- `__user_thread_start = _start`
- `__user_init_process_entry = _start`

The user virtual image begins at `0x10000`. That keeps address 0 unmapped so
null-pointer mistakes are more likely to fault instead of silently accessing
valid memory. It also gives the kernel a fixed low user base for ELF segments.

Program headers use ELF-style permissions:

| PHDR | Flags | Meaning |
|---|---:|---|
| `text` | `5` | Read + execute user code. |
| `rodata` | `4` | Read-only constants. |
| `data` | `6` | Read + write initialized data and BSS memory. |

Sections are aligned to 4096 bytes so the kernel can map them using normal page
granularity and preserve segment permissions cleanly. `.bss` is `NOLOAD`: it
occupies virtual memory in the process, but it does not add zero bytes to the
ELF file.

`user/linker.ld` follows the same address layout but uses `ENTRY(user_thread_start)`
and exports `__user_init_process_entry = init_process_entry`. The active Makefile
uses `user/user_linker.ld` for command ELFs; the older script remains useful as
documentation for the direct function-entry model the process code originally
supported.

## Userspace Startup Code

Every command ELF includes `user/user_boot.S`. This file defines `_start`, which
is the real userspace entry point.

The kernel enters a new program with:

- `x0 = argc`
- `x1 = argv`
- `sp = user stack pointer`
- `elr = ELF entry point`

The `_start` stub:

1. Saves `argc` and `argv` on the user stack.
2. Initializes the userspace heap with `mem_init`.
3. Restores `argc` and `argv`.
4. Calls `main(argc, argv)`.
5. Calls `exit` with `main`'s return value.
6. Spins only if `exit` unexpectedly returns.

The heap range passed to `mem_init` is:

```
0x400000 - 0x404000
```

That matches the userspace heap constants in the memory layout. Pages in that
range are still faulted in lazily by the kernel as the process touches them.

## Embedding User Programs in the Kernel Image

The build embeds userspace into the kernel image in two related ways.

### Raw Initial User Image

The Makefile first builds `build/<platform>/user/bin/init.elf`. It then creates:

```
build/<platform>/user/init.bin
```

with `objcopy -O binary`. That flat binary is included by generated assembly:

```
.section .user_image, "a"
.balign 4096
.incbin "<absolute path to init.bin>"
.balign 4096
```

The root `linker.ld` places `.user_image` into the kernel image, aligned to a
page boundary, and exports:

- `__user_image_start`
- `__user_image_end`
- `__user_image_start_phys`
- `__user_image_end_phys`

The final kernel page table maps this region read-only. This keeps the raw
initial user image available as kernel-owned data without making it writable
kernel state.

The Makefile also runs `nm` on `init.elf` and generates
`build/<platform>/user/user_image.h`. Symbols beginning with `__user_` become
kernel C macros such as:

- `USER_IMAGE_START`
- `USER_IMAGE_END`
- `USER_THREAD_START`
- `USER_INIT_PROCESS_ENTRY`

Process bootstrap code includes this generated header so it can create an
initial trap frame with the correct user entry address.

### Embedded Command ELF Blobs

Every command ELF is also embedded into the kernel image as raw bytes. The
Makefile generates `user_bins.S` with one `.incbin` range per command:

```
__user_bin_<name>_start:
  .incbin "build/<platform>/user/bin/<name>.elf"
__user_bin_<name>_end:
```

It also generates `user_bins.c`, which builds a table:

```c
const user_bin_t user_bins[] = {
    { "/bin/<name>", __user_bin_<name>_start, __user_bin_<name>_end },
};
```

Those objects are linked into the kernel as read-only data. The bytes are the
actual ELF files, not parsed program segments. The runtime ELF loader is only
used later when a process execs one of the files from `/bin`.

## Seeding `/bin`

After the filesystem is created or mounted, the kernel seeds userspace commands
into `/bin`.

The seeding path:

1. Ensures `/bin` exists.
2. Iterates `user_bins`.
3. For each entry, checks whether the target path already exists.
4. If missing, creates the file and writes the embedded ELF bytes into it.
5. Marks the file executable.
6. If the file already exists, refreshes the executable bit.

During `mkfs`, `seed_user_bins_for_mkfs()` temporarily marks the filesystem
mounted enough to use the normal KAPI/OFT write path, writes the embedded
programs, then flushes caches and unmounts inode state. During a normal mount,
`seed_user_bins()` refreshes `/bin` after the superblock and root directory are
validated.

This design makes userspace reproducible. The kernel image contains the command
ELFs needed to populate a fresh filesystem, but commands still execute from the
filesystem after boot.

## EL0 Isolation

Userspace runs at EL0. Kernel code runs at EL1. User programs cannot directly:

- execute privileged instructions.
- access EL1 system registers.
- access device MMIO.
- write kernel memory.
- call kernel C functions.

The separation is enforced by exception level and page-table permissions.

The kernel uses:

- `TTBR1_EL1` for the high-half kernel address space.
- `TTBR0_EL1` for the active user process address space.

User-accessible pages are mapped with EL0 permissions. Kernel text, kernel data,
device memory, and per-process kernel stack pages use EL1-only permissions.
Attempts to access unmapped or disallowed addresses enter the trap path as data
or instruction aborts.

This means the only intentional way from userspace into the kernel is the
syscall ABI. A user program asks for services by placing a syscall number in
`x8`, arguments in `x0` through `x5`, and executing `svc #0`.

## Process Address Space

Each process owns a user page table tracked in its PCB:

- `ttbr0_el1`: physical address written to `TTBR0_EL1`.
- `ttbr0_el1_va`: kernel virtual address of the page table.

When a process is created, the kernel allocates a fresh user L0 page table and
registers it with the page-table metadata system. The process's main thread is
given a trap frame near the top of its per-process kernel stack region. That
trap frame is what `trap_frame_restore` eventually uses to enter EL0.

Important virtual regions are:

| Region | Address |
|---|---:|
| User image base | `0x10000` |
| User heap start | `0x400000` |
| User heap size | `0x4000` |
| User stack top | `0x800000` |
| User stack size | `0x2000` |
| Per-process kernel stack top | `0x900000` |
| Per-process kernel stack size | `0x2000` |

The user stack and heap are normal user memory. The per-process kernel stack
region is used by kernel trap/context machinery and is mapped with kernel-only
permissions.

ELF segments are not eagerly copied into anonymous memory at exec time. Instead,
the exec path records segment metadata in the process page-table structure.
Instruction and data abort handlers demand-load pages from the executable file
when the process first touches them.

## Syscall Boundary

Userspace syscall wrappers live in `user/lib/syscall.h`,
`user/lib/syscall.c`, `user/lib/fs_syscall.h`, `user/lib/signals.h`, and
`user/lib/tty_syscall.h`.

The wrappers are intentionally thin. They hide register setup and syscall
numbers from command code, but they do not implement kernel policy. The generic
wrapper places:

- syscall number in `x8`.
- arguments in `x0` through `x5`.
- the return value in `x0`.

Then it executes:

```asm
svc #0
```

The kernel receives this as a lower-EL synchronous exception, validates that it
is an `SVC64` path, and dispatches it through `syscall_dispatch`.

Function-by-function syscall and library behavior belongs in
`docs/api-docs/user-api.md`; this document only describes the architecture of
the boundary.

## Init Process

`processes_init()` creates PID 0 as the init process. The initial process is
created with `USER_INIT_PROCESS_ENTRY`, then the kernel immediately attempts to
replace it with `/bin/init` through `k_exec_process`.

This gives the kernel two useful properties:

- there is a known generated entry symbol available during process bootstrap.
- the actual long-running init program is the ELF stored in the filesystem at
  `/bin/init`.

The userspace `init` program ignores all signals, starts the first shell for
TTY 0, waits for terminal-create requests, and spawns additional shells for new
TTY tabs. It also waits for child processes so orphaned or exited children are
reaped through the normal process model.

## Shell and Command Execution

The shell is a normal userspace ELF program. It is larger than most commands, so
the Makefile links it with extra objects from `user/cmds/shell`.

At startup, the shell:

1. Parses the TTY number from `argv[1]`.
2. Opens `/dev/ttyN` for stdin, stdout, and stderr.
3. Installs signal handlers for interactive job control.
4. Blocks job-control signals that should not stop the shell itself.
5. Calls `tcsetpgrp` to take foreground control of its TTY.
6. Enters the prompt loop.

For external commands, the shell searches executable paths, forks child
processes, sets up pipes/redirection when needed, and calls `exec`. Builtins and
parser details are shell architecture concerns, but the important OS boundary is
that commands run as separate userspace processes through the same syscall and
ELF execution path as `init` and `shell`.

## Userspace Heap

Userspace has its own small allocator in `user/lib/malloc.c`. `_start` calls
`mem_init` before `main`, giving the allocator the fixed heap range:

```
USER_HEAP_START = 0x400000
USER_HEAP_SIZE  = 0x4000
```

The allocator is a segregated explicit free-list allocator with headers,
footers, splitting, coalescing, and multiple size classes. It manages user
virtual memory inside the process, while the kernel decides when backing pages
actually exist. If the allocator touches an unmapped heap page, the MMU fault
handler can lazily allocate it.

This split mirrors the rest of userspace: user code manages its own abstractions
inside its allowed virtual range, but the kernel owns the page tables and
physical memory.

## User Libraries

`user/lib` provides a minimal libc-like layer:

- syscall wrappers.
- filesystem wrappers.
- signal wrappers.
- TTY wrappers.
- `stdio` helpers.
- string/memory helpers.
- userspace malloc.
- errno formatting.
- test helpers.

The libraries are statically linked into every command. There is no dynamic
linker and no shared library loading at runtime. This makes each command ELF
self-contained, which simplifies `/bin` seeding and exec.

## Design Tradeoffs and Limits

The userspace design favors simple, inspectable binaries over a full Unix user
environment.

Current limits include:

- statically linked command ELFs only.
- no dynamic loader.
- no shared libraries.
- fixed user virtual layout.
- small fixed default userspace heap.
- build-time command discovery from `user/cmds/*.c`.
- embedded command set is refreshed by rebuilding the kernel image.
- userspace starts through a minimal `_start` stub rather than a full C runtime.

These constraints keep the implementation understandable while still covering
the important OS mechanics: separate EL0 execution, syscall entry, per-process
page tables, ELF command images, filesystem-backed exec, shell job control, and
kernel-populated `/bin` programs.
