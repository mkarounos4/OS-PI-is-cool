# ELF Loading and Exec Architecture

This document describes how filesystem-backed userspace ELF binaries are loaded
and executed through `exec`. The build and packaging side is covered in
[userspace.md](userspace.md); this document focuses on the runtime loader, page
table replacement, lazy segment loading, and process state transitions.

## List of Features

- [System structure](#system-structure)
- [Why ELF-backed exec matters](#why-elf-backed-exec-matters)
- [Runtime entry points](#runtime-entry-points)
- [Supported ELF format](#supported-elf-format)
- [Exec flow](#exec-flow)
- [Argument copying and user stack layout](#argument-copying-and-user-stack-layout)
- [Page table replacement](#page-table-replacement)
- [Lazy segment registration](#lazy-segment-registration)
- [Demand paging from ELF files](#demand-paging-from-elf-files)
- [Trap frame handoff](#trap-frame-handoff)
- [State preserved and replaced by exec](#state-preserved-and-replaced-by-exec)
- [Init and shell integration](#init-and-shell-integration)
- [Error handling and commit policy](#error-handling-and-commit-policy)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)

## System Structure

```
+--------------------------------------------------+
| user shell / init / command calls exec(path,argv) |
+--------------------------------------------------+
| syscall dispatcher S_EXEC                         |
+--------------------------------------------------+
| k_exec / k_exec_process                           |
+--------------------------------------------------+
| elf_exec_process                                  |
+--------------------------------------------------+
| VFS/OFT reads ELF headers and program headers     |
+--------------------------------------------------+
| new user page table + exec stack + segment table  |
+--------------------------------------------------+
| trap frame rewritten to argc/argv and ELF entry   |
+--------------------------------------------------+
| page faults demand-load PT_LOAD pages from inode  |
+--------------------------------------------------+
```

# Detailed Architecture and Decisions

## Why ELF-Backed Exec Matters

Earlier process creation can start a process at an in-memory function pointer
through `spawn`, but the normal userspace model is now filesystem-backed ELF
execution. That matters because it gives the OS the classic Unix workflow:

```
shell
  -> fork child
  -> child configures descriptors, process group, and terminal state
  -> child execs /bin/<command>
  -> kernel replaces the child image with an ELF from the filesystem
```

This design makes commands real files under `/bin`, not hardcoded kernel
functions. It also forces the same VFS, permission, page-table, trap-frame, and
userspace startup paths to work for init, shell commands, tests, pipelines, and
scripts.

## Runtime Entry Points

There are two kernel entry points into the same ELF loader:

| Entry point | Used by | Behavior |
|---|---|---|
| `k_exec(path, argv, frame, next_frame)` | `S_EXEC` syscall from the current process | Replaces the current process image and installs the new TTBR0 immediately. |
| `k_exec_process(pid, path, argv)` | Kernel-managed replacement, notably booting `/bin/init` | Replaces the target process image using its saved main-thread trap frame without immediately installing TTBR0. |

Both paths call `elf_exec_process(...)`. Keeping one core loader avoids
separate boot-time and syscall-time ELF semantics.

## Supported ELF Format

The runtime loader accepts a deliberately narrow ELF subset:

- 64-bit ELF (`ELFCLASS64`).
- little-endian data encoding.
- current ELF version.
- executable file type (`ET_EXEC`).
- AArch64 machine type.
- at least one program header.
- program header entries exactly matching the loader's `elf64_phdr_t` layout.

Only `PT_LOAD` program headers are registered as loadable user segments. Other
program header types are skipped.

This is intentionally simpler than a production loader. There is no dynamic
linker, relocation processing, shared library loading, interpreter path,
position-independent executable support, or auxiliary vector setup. The build
produces static freestanding command ELFs that match this loader.

## Exec Flow

`elf_exec_process` performs the replacement in stages:

1. Validate `pcb`, `path`, and the incoming trap frame.
2. Resolve the executing thread. If the current thread is not the target
   process, fall back to the target process's main thread.
3. Detach and terminate sibling threads, because `exec` replaces the process
   image and continues with one executing thread.
4. Copy `argv` strings into kernel memory before destroying or replacing any
   address-space state.
5. Derive the process display name from the executable path.
6. Open the ELF with `k_open(path, O_RDONLY)`.
7. Resolve the OFT entry and verify execute permission on the inode.
8. Read and validate the ELF header.
9. Create a new user page table.
10. Map the process kernel-stack/trap-frame range into the new page table.
11. Build the new user stack containing argument strings and the `argv` array.
12. Read program headers and register every valid `PT_LOAD` segment.
13. Close the executable file.
14. Copy the existing kernel-stack pages into the new table so the replacement
    trap frame can be edited safely.
15. Rewrite the trap frame for the new program.
16. Update the thread context and PCB TTBR0 fields.
17. Optionally install the new TTBR0 and destroy the old page table.
18. Collapse the PCB thread list to the exec thread.
19. Reset signal dispositions according to exec rules.

The key design decision is that the loader builds and validates replacement
state before committing it into the PCB. A failed `exec` returns an error to the
old process image instead of leaving the process half-replaced.

## Argument Copying and User Stack Layout

The loader copies arguments in two phases.

First, `copy_exec_args` copies userspace-provided strings into kernel memory.
This protects the loader from losing access to `argv` after the address space is
replaced. The limits are:

| Limit | Value |
|---|---:|
| Maximum argument count | 32 |
| Maximum bytes per argument string | 256 including terminator requirement |

Second, `setup_exec_stack` writes the copied strings into the new user stack
from high addresses downward. It then aligns the stack to 16 bytes, writes the
`argv` pointer array, appends a NULL terminator, and returns:

- the final user `sp`.
- the user virtual address of `argv`.

On entry to the new program:

- `x0 = argc`
- `x1 = argv`
- `sp = final user stack pointer`
- `elr = ELF header entry point`

The userspace `_start` stub receives those values, initializes the userspace
heap, calls `main(argc, argv)`, and exits with `main`'s return value.

## Page Table Replacement

`exec` creates a fresh TTBR0 user page table instead of modifying the old one in
place. The new table receives:

- the fixed process kernel-stack/trap-frame mapping.
- the user stack pages needed to hold `argc` and `argv`.
- lazy segment metadata for ELF `PT_LOAD` regions.

The old table remains active until the new state is ready. For syscall-driven
`exec`, the loader installs the new TTBR0, invalidates user TLB state, and then
destroys the old table. For kernel-managed `k_exec_process`, the loader updates
the saved thread/PCB context but leaves actual TTBR0 installation for the later
context switch.

This split exists because syscall `exec` is replacing the currently running
address space, while boot-time init replacement may happen before that process
is actually running on the CPU.

## Lazy Segment Registration

The loader does not eagerly copy every ELF segment into anonymous pages.
Instead, each `PT_LOAD` header becomes a memory segment record in page-table
metadata:

- executable inode ID.
- file offset.
- file-backed size.
- target virtual address.
- physical address field from the program header.
- memory size.
- ELF flags.

The runtime page table owns this segment list. When the process later faults on
a segment address, the memory subsystem can use the segment metadata to load
only the required page.

This is a deliberate performance and clarity tradeoff. Exec stays fast and
small, while page-fault handling demonstrates demand paging from a real file.

## Demand Paging From ELF Files

Instruction and data abort handlers call `load_segment_page_for_fault` when a
faulting address may belong to a registered user segment.

The demand-loading path:

1. Find a segment whose virtual range contains the faulting address.
2. Check whether the fault type is allowed by the segment flags.
3. Allocate one physical page.
4. Read the relevant bytes from the executable inode.
5. Leave bytes beyond `p_filesz` zero-filled for BSS-like memory.
6. Map the page at the faulting page virtual address with attributes derived
   from ELF flags.
7. Return to the faulting context after TLB invalidation.

Segment permissions are derived as:

| ELF flags | Mapping |
|---|---|
| writable segment | `ATTR_USER_RW` |
| executable non-writable segment | `ATTR_USER_RX` |
| otherwise | `ATTR_USER_RO` |

Instruction faults require `PF_X`. Data faults require `PF_R` or `PF_W`. A
fault that hits a known segment with the wrong access type is treated as a
permission fault rather than silently mapping the page.

## Trap Frame Handoff

The loader edits a trap frame in the process's kernel-stack mapping. After a
successful exec, the frame is rewritten so the next trap return enters the new
ELF image:

- `elr` is set to `e_entry`.
- `sp` is set to the new user stack pointer.
- `x0` is set to `argc`.
- `x1` is set to the user `argv` pointer.
- general registers are cleared.
- diagnostic fields such as `esr`, `far`, `type`, and `intid` are cleared.

For syscall `exec`, the syscall dispatcher returns the replacement trap frame to
the exception path. The vector restore code then restores that frame and
executes `eret`, so userspace resumes in the new program instead of returning to
the old caller.

## State Preserved and Replaced by Exec

Preserved:

- PID, PPID, PGID, and parent-child relationships.
- current working directory.
- open file descriptors.
- ignored signal dispositions, except uncatchable signals.

Replaced:

- user address space.
- user stack.
- lazy memory segment list.
- trap-frame entry state.
- process name.
- caught signal handlers.
- sibling threads.

`SIGKILL` and `SIGSTOP` are always reset to default behavior. Ignored handlers
for other signals remain ignored, while caught handlers are reset because their
function pointers belonged to the old image.

## Init and Shell Integration

During boot, the kernel creates the initial process and then runs:

```
k_exec_process(pid, "/bin/init", { "/bin/init", NULL })
```

That means init uses the same ELF loader as every other command.

The shell uses the standard Unix-style sequence for commands:

```
fork()
  child:
    setpgid / dup2 / close as needed
    exec("/bin/<command>", argv)
  parent:
    waitpid or track background job
```

This is why `fork` and `exec` are separate. `fork` gives the child a copy of the
shell's descriptor and process-group context; `exec` replaces only the child
image after the child has customized that context.

## Error Handling and Commit Policy

`exec` can fail before commit for these reasons:

- invalid target process or trap frame.
- too many arguments or an argument string that is too long.
- file open failure.
- missing OFT entry.
- execute permission failure.
- empty or unreadable file.
- invalid ELF header.
- invalid program header bounds.
- page-table allocation failure.
- stack setup failure.
- segment registration failure.
- file close failure before commit.

The loader frees copied arguments, closes the executable when needed, and
destroys the new page table on failure after allocation. It updates the PCB and
thread context only after the new trap frame and segment metadata are ready.

This gives `exec` its important user-visible property: success does not return
to the old program, but failure returns an error to the old program.

## Design Tradeoffs and Limits

- Only static AArch64 executable ELFs are supported.
- No dynamic linker, shared libraries, relocations, PIE, interpreter segments,
  or auxiliary vectors are implemented.
- Segment pages are demand-loaded, which improves exec latency but makes page
  fault handling part of normal program startup.
- Argument limits are fixed and small, matching the current shell environment.
- `p_paddr` is recorded from the program header for metadata completeness, but
  user mappings are driven by virtual addresses and allocated physical pages.
- The loader uses the filesystem and inode APIs directly; executable bytes are
  ordinary files under `/bin`, not special kernel symbols.
- The implementation favors understandable Unix-like semantics over full ELF
  ABI completeness.
