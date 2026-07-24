# Process Management Overview

This document describes the process, thread, scheduler, signal, and job-control
architecture of the OS. It focuses on how runnable work is represented, how the
kernel moves between threads, how Unix-like lifecycle operations are modeled,
and why the implementation chooses these boundaries instead of larger
production-kernel machinery.

The process subsystem sits between several other documented areas:

- Boot, traps, timer interrupts, and syscall entry are covered in
  [architecture.md](architecture.md).
- User page tables, lazy allocation, and Copy-on-Write are covered in
  [memory.md](memory.md).
- ELF program packaging and userspace startup are covered in
  [userspace.md](userspace.md).
- File descriptors, open-file references, pipes, `/proc`, and `/dev` are
  covered in [filesystem.md](filesystem.md) and
  [device-drivers.md](device-drivers.md).
- Signal architecture is covered in [signals.md](signals.md), while public
  syscall and signal behavior is covered in
  [syscall-table.md](../api-docs/syscall-table.md) and
  [signals-api.md](../api-docs/signals-api.md).

## List of Features

- [System structure](#system-structure)
- [Process subsystem scope](#process-subsystem-scope)
- [Ownership boundaries](#ownership-boundaries)
- [Core execution flows](#core-execution-flows)
- [Process model](#process-model)
- [Process control block](#process-control-block)
- [Thread model](#thread-model)
- [PID and TID allocation](#pid-and-tid-allocation)
- [Process and thread states](#process-and-thread-states)
- [Scheduler design](#scheduler-design)
- [Run queues and scheduling policy](#run-queues-and-scheduling-policy)
- [Priority management](#priority-management)
- [Yield and dispatch points](#yield-and-dispatch-points)
- [Context switching](#context-switching)
- [Trap frame and saved register state](#trap-frame-and-saved-register-state)
- [Kernel stacks](#kernel-stacks)
- [User process entry state](#user-process-entry-state)
- [Process address-space ownership](#process-address-space-ownership)
- [Page table lifetime](#page-table-lifetime)
- [Initial process bootstrap](#initial-process-bootstrap)
- [Process creation](#process-creation)
- [Fork semantics](#fork-semantics)
- [Exec state replacement](#exec-state-replacement)
- [Argument vector handoff](#argument-vector-handoff)
- [Exit semantics](#exit-semantics)
- [Wait and child reaping](#wait-and-child-reaping)
- [Parent-child relationships and orphans](#parent-child-relationships-and-orphans)
- [Process groups and job control](#process-groups-and-job-control)
- [Per-process resource ownership](#per-process-resource-ownership)
- [Blocking and wakeup paths](#blocking-and-wakeup-paths)
- [Signals](#signals)
- [Sleep integration](#sleep-integration)
- [Multithreading and synchronization](#multithreading-and-synchronization)
- [System call integration](#system-call-integration)
- [Subsystem integration boundaries](#subsystem-integration-boundaries)
- [Error handling and cleanup policy](#error-handling-and-cleanup-policy)
- [Testing and debugging hooks](#testing-and-debugging-hooks)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)
- [Documentation gaps](#documentation-gaps)

## System Structure

```
+--------------------------------------------------+
|        EL0 programs: init, shell, commands        |
+--------------------------------------------------+
| fork/exec/wait/exit/signals/tty syscalls          |
+--------------------------------------------------+
| process table, thread table, process groups       |
+--------------------------------------------------+
| priority run queues, timer preemption, idle task  |
+--------------------------------------------------+
| trap frames, per-thread context, TTBR0 switching  |
+--------------------------------------------------+
| VM, filesystem, VFS/OFT, TTY, timer, IRQ          |
+--------------------------------------------------+
```

# Detailed Architecture and Decisions

## Process Subsystem Scope

The process subsystem owns the kernel's view of runnable user programs. Its
main responsibilities are:

- allocate and recycle process IDs.
- track parent-child relationships.
- create and destroy process address spaces.
- create main threads and attach them to scheduler queues.
- implement `fork`, `exec`, `exit`, `waitpid`, `spawn`, `setpgid`, `getpgrp`,
  `dup2`, `ps`, and process priority changes.
- maintain process-level signal state and per-thread signal masks.
- coordinate with TTY job control and `/proc` process reporting.

The process layer intentionally does not parse shell syntax, load boot media,
perform raw disk I/O, or handle page faults directly. Those jobs belong to
userspace, filesystem/device code, and memory management respectively.

The design follows the Unix idea that a process is the unit of resource
ownership while a thread is the unit scheduled onto the CPU. Even though most
userspace programs currently run with one thread, keeping those concepts
separate lets process-level APIs such as `exit`, `waitpid`, signals, and file
descriptors behave cleanly as thread support grows.

## Ownership Boundaries

| Owner | State owned | Reason |
|---|---|---|
| Process table | PID, PPID, PGID, name, lifecycle state, children, cwd, file descriptors, signal actions, TTBR0 | Keeps Unix process resources in one inspectable structure. |
| Thread table | TID, CPU context, kernel stack, thread state, priority, signal mask, pending thread signals, blocked-event fields | Lets the scheduler operate on runnable execution contexts instead of whole processes. |
| Scheduler | Ready queues, current thread, timer quantum handoff, idle context | Keeps dispatch policy separate from process lifetime policy. |
| Memory manager | Page table allocation, page-table metadata, CoW refcounts, lazy page fault handling | Avoids duplicating page-table rules in process code. |
| Filesystem/OFT | Open-file table entries, inode references, path lookup, ELF reads | Lets `fork` inherit descriptors without making process code understand every file type. |
| TTY layer | Foreground process group and terminal session state | Keeps job-control terminal rules out of the generic scheduler. |

The tradeoff is that lifecycle operations cross subsystem boundaries. For
example, `exec` opens an ELF through the filesystem, creates a fresh user page
table through memory management, edits a trap frame from the trap layer, and
then updates process/thread state. The benefit is that each subsystem still
owns the invariants it can validate best.

## Core Execution Flows

### Boot to Init

`kernel_main` initializes hardware, exceptions, IRQs, timers, final kernel page
tables, heap/page metadata, persistent storage, root filesystems, `/proc`,
`/dev`, character devices, and signals before starting the scheduler. Only
after those dependencies exist does it call `scheduler_init()` and
`scheduler_start()`.

`scheduler_init()` creates the thread ready queues, initializes the thread and
process tables, creates an idle context, and calls `processes_init()`.
`processes_init()` creates PID 0, then attempts to `exec` `/bin/init` from the
filesystem. The resulting init thread is added to the scheduler, so
`scheduler_start()` can switch away from the boot context into normal scheduled
execution.

This order is deliberate. Init is a normal userspace program loaded from `/bin`,
not a privileged hardcoded shell path. That forces early boot to bring up the
same filesystem and ELF-loading path that later user programs use.

### Runnable Process to CPU

Runnable work enters the scheduler as `THREAD_READY` TCBs. The scheduler picks
a thread from one of three priority queues, marks it `THREAD_RUNNING`, loads
that thread's saved context, and restores the thread's TTBR0 user address space.

If no ready thread exists, the scheduler switches to an idle context that loops
with `wfe`. This is simpler than inventing a fake idle process and keeps idle
execution outside the normal process table.

### User Trap to Kernel Return

Userspace enters the kernel with `svc #0`. The trap path saves user registers
into a trap frame, the syscall dispatcher handles the requested service, and
the return path restores the frame to EL0. For syscalls such as `fork` and
`exec`, the process layer edits the saved frame so the resumed user instruction
stream observes the correct return value, entry point, stack pointer, and
arguments.

The full syscall path is:

```
EL0 program
  -> user syscall wrapper loads x8 and x0-x5
  -> svc #0
  -> lower-EL synchronous exception vector
  -> exception_entry saves a trap_frame on the current stack
  -> exception_dispatch
  -> handle_sync_exception
  -> syscall_dispatch
  -> process/filesystem/timer/signal implementation
  -> selected trap_frame restored by vector code
  -> eret back to EL0
```

Most syscalls return the same trap frame they entered with. A few paths can
change control flow: `fork` edits the child trap frame, `exec` may return a new
trap frame from the replacement address space, and blocking/yielding syscalls
may leave the current thread in the scheduler before the original frame is
restored.

### Timer IRQ to Context Switch to User Return

Preemption starts as a hardware timer IRQ while either userspace or kernel code
is running. The vector entry saves a trap frame for the interrupted context,
then `exception_dispatch()` calls `irq_handle_exception()`. The timer driver
runs expired software timer callbacks, including the scheduler quantum callback
that marks scheduling as needed. After IRQ dispatch completes,
`exception_dispatch()` calls `run_scheduler_if_needed()`.

When scheduling is needed:

1. `scheduler_tick()` moves the old running thread back to `THREAD_READY` when
   it is still runnable.
2. `get_next_thread()` picks the next ready TCB from the priority queues.
3. `context_switch(old_ctx, new_ctx)` saves callee-saved registers, saves the
   old stack pointer and TTBR0, restores the new thread's registers, installs
   the new TTBR0, invalidates user TLB state, restores the new stack pointer,
   and returns into the resumed kernel path for that thread.
4. The resumed path eventually reaches the vector restore code or
   `trap_frame_restore()`.
5. The chosen trap frame restores `ELR_EL1`, `SPSR_EL1`, `SP_EL0` when
   returning to userspace, general registers, and finally executes `eret`.

This means the scheduler does not directly jump to arbitrary user code. It
switches kernel contexts first; the resumed kernel context returns to user mode
only through a saved trap frame. That keeps context switching and privilege
return separate.

### Blocking to Wakeup

A blocking syscall or driver path marks the current thread blocked, stores the
event it is waiting for when applicable, and yields. Wakeup paths call
`send_unblock_event()` or directly move a waiting thread back to
`THREAD_READY` and reinsert it into the scheduler.

This design is intentionally event-bit based rather than wait-channel based.
It is small and easy to inspect, but it means the kernel has only coarse public
blocking events such as child changes, timer expiration, signal interruption,
and TTY requests.

### Exit to Reap

`exit` stores the process exit code, terminates sibling threads, and terminates
the current thread. Process state is derived from thread counters: if all
threads are gone or zombie, the process becomes a zombie and the parent is
notified with `SIGCHLD`. The process table slot is not freed until a parent
successfully reaps it with `waitpid`, preserving status for Unix-like child
collection.

## Process Model

A process is represented by a fixed-slot PCB in a `MAX_PROCESS_COUNT` process
array. PID values are stable indexes into that array. A PCB owns:

- identity: `pid`, `ppid`, `pgid`, and a short `name`.
- lifecycle state: running, blocked, stopped, zombie, or unused.
- exit status and wait notification flags.
- the vector of child PIDs.
- the vector of thread IDs.
- the user page-table root in both physical and kernel-virtual form.
- userspace file-descriptor mappings to kernel open-file table descriptors.
- current working directory inode.
- process-level signal actions and pending process signals.
- child wait queue for threads blocked in `waitpid`.
- thread-state counters used to derive process state.

Using a fixed process table is a conscious educational tradeoff. It makes PID
lookup O(1), keeps `/proc` generation simple, and avoids early allocator
fragmentation issues. The cost is a hard process limit and no PID generation
counter, so the system does not attempt to solve all stale-PID edge cases that
a production Unix kernel would.

## Process Control Block

The PCB is the resource owner. Its most important fields are grouped by purpose:

| Field group | Fields | Purpose |
|---|---|---|
| Identity | `pid`, `ppid`, `pgid`, `name` | Process lookup, parent tracking, job control, `/proc` display. |
| Lifecycle | `state`, `exit_code`, wait pending flags | Process status and wait reporting. |
| Threads | `tids`, thread counters | Connects process state to the scheduled TCBs. |
| Address space | `ttbr0_el1`, `ttbr0_el1_va` | User page-table root for context switches and page-table cleanup. |
| Resources | `file_descriptors`, `cwd` | Per-process filesystem view. |
| Signals | `sigactions`, `pending_signals` | Process-directed signal behavior. |
| Relationships | `children`, `child_waitq` | `waitpid`, orphan adoption, parent notification. |

Process state is not manually assigned in every path. The helper
`pcb_thread_change_state()` updates per-state thread counters and then derives
the aggregate process state. This avoids having the process say "running" while
all of its threads are actually stopped or blocked.

## Thread Model

A thread is represented by a TCB. The scheduler runs threads, not PCBs. Each
TCB owns:

- a globally allocated TID.
- current thread state.
- saved CPU context.
- pointer back to its owning PCB.
- a kernel stack.
- the user stack top used for process entry.
- pending thread-directed signals and a signal mask.
- scheduler priority.
- blocked-event metadata used by event waits and `waitpid`.

The current implementation supports multiple TCBs per process at the kernel
data-structure level. `exec` collapses a process back to the executing thread
by terminating and detaching sibling threads before replacing the address
space. That matches the practical Unix rule that `exec` replaces the program
image and does not carry arbitrary old thread execution forward.

## PID and TID Allocation

PIDs are allocated by scanning for the next `PROC_UNUSED_STATE` PCB in the
fixed process array. During initialization, each slot's PID is set to its array
index.

TIDs are allocated from a growable vector of TCB pointers. `thread_create()`
reuses a `THREAD_UNUSED` slot when available, otherwise it appends a new TCB.

The benefit of this design is straightforward lookup and easy `/proc`
enumeration. The tradeoff is that allocation is linear in the table/vector size
and there is no namespace, generation number, or security boundary around PID
reuse.

## Process and Thread States

Process states:

| Process state | Meaning |
|---|---|
| `PROC_UNUSED_STATE` | PCB slot is free. |
| `PROC_RUNNING_STATE` | At least one thread is ready or running. |
| `PROC_BLOCKED_STATE` | No running threads exist, but at least one thread is blocked. |
| `PROC_STOPPED_STATE` | No running/blocking threads exist, but at least one thread is stopped. |
| `PROC_ZOMBIE_STATE` | The process has no live runnable/blocking/stopped work and is waiting to be reaped. |

Thread states:

| Thread state | Meaning |
|---|---|
| `THREAD_UNUSED` | TCB slot is free. |
| `THREAD_READY` | Eligible to be selected by the scheduler. |
| `THREAD_RUNNING` | Currently executing. |
| `THREAD_STOPPED` | Stopped by a signal or synchronization path. |
| `THREAD_ZOMBIE` | Terminated and awaiting cleanup. |
| `THREAD_BLOCKED_INTERRUPTABLE` | Blocked, but ordinary signals can interrupt the wait. |
| `THREAD_BLOCKED_KILLABLE` | Blocked, but terminating signals can interrupt the wait. |
| `THREAD_BLOCKED_UNINTERUPTABLE` | Blocked until the requested event occurs. |

Keeping stopped, blocked, and zombie states distinct is necessary for
`waitpid(WUNTRACED)`, `waitpid(WCONTINUED)`, terminal job control, and child
reaping.

## Scheduler Design

The scheduler is preemptive and single-core. A periodic ARM generic timer event
sets a scheduling flag; trap/interrupt return paths can then call
`run_scheduler_if_needed()` and switch threads. Threads may also yield
explicitly through `S_YIELD`, blocking operations, sleep, or signal-driven state
changes.

The scheduler is deliberately small:

- one global `curr_thread`.
- three global ready queues.
- a boot context used for the first switch.
- an idle context used when no TCB is runnable.
- a timer quantum configured by `SCHEDULER_QUANTUM_MS`.

This matches the OS's current single-core architecture. Per-CPU run queues,
load balancing, CPU affinity, and inter-processor interrupts would add
complexity without benefit until multicore support exists.

## Run Queues and Scheduling Policy

Ready threads are stored in three priority queues. `add_thread_to_scheduler()`
inserts a ready thread at the front of its priority queue and avoids inserting
the same TCB twice. `get_next_thread()` pops from the back, skips stale entries
that are no longer ready, and rotates across priority queues.

Each priority queue has a counter limit:

| Priority index | Counter limit |
|---:|---:|
| 0 | 9 |
| 1 | 6 |
| 2 | 4 |

After a queue has supplied more than its configured count, the scheduler rotates
to the next priority. This gives higher-priority work more CPU opportunities
while still allowing lower-priority queues to make progress.

The policy is intentionally approximate rather than a full Unix nice/CFS model.
It is easy to reason about and debug in a teaching OS, but it does not provide
precise fairness, runtime accounting, or starvation proofs.

## Priority Management

Threads default to priority `1`. `proc_change_priority(pid, new_priority)`
changes every thread in the target process to priority `0`, `1`, or `2`.
Passing PID `0` targets the current process.

The syscall wrapper exposes this as a process-level operation because that is
the interface users naturally expect from a shell command. Internally, the
scheduler remains thread-based, so the implementation applies the process
priority to each owned TCB.

## Yield and Dispatch Points

Scheduling can happen at these points:

- timer quantum expiration.
- explicit `S_YIELD`.
- `sleep` and event-blocking syscalls.
- blocking filesystem, TTY, pipe, or synchronization paths.
- stop/terminate signal handling when it affects the current thread.
- scheduler start after boot.

This gives the OS preemptive multitasking without requiring every kernel path
to poll for reschedule decisions constantly.

## Context Switching

Each TCB stores a `struct cpu_context` containing the callee-saved register
state required to resume kernel execution for that thread. It also stores the
physical and virtual form of the process's TTBR0 page-table root.

The scheduler saves the old thread context, loads the next context, and lets
the assembly context-switch path restore execution. User-mode return is handled
by trap-frame restore, not by directly jumping to EL0 from C.

This split keeps two different state types separate:

- CPU context: kernel scheduling state used between kernel threads.
- Trap frame: user register state used when returning from exceptions/syscalls.

## Trap Frame and Saved Register State

The trap frame is stored inside the process's mapped kernel-stack region. For a
fresh process, `setup_process_main_thread()` initializes the frame so the first
return to userspace enters the userspace bootstrap at `USER_THREAD_START` with
the requested function pointer and argument in registers.

`fork` copies the parent's trap frame into the child and changes only `x0` to
`0`, which gives the child the normal Unix return value. The parent receives the
child PID as the syscall return value.

`exec` rewrites the frame more aggressively:

- `elr` becomes the ELF entry point.
- `sp` becomes the newly constructed user stack.
- `x0` becomes `argc`.
- `x1` becomes `argv`.
- general registers are cleared.

This frame-centered design lets lifecycle syscalls change future userspace
execution without requiring special cases in the generic trap return path.

## Kernel Stacks

Each thread receives an allocator-backed kernel stack. Process creation also
maps a per-process kernel-stack virtual range from
`PROC_KERNEL_STACK_TOP - PROC_KERNEL_STACK_SIZE` to `PROC_KERNEL_STACK_TOP` into
the process page table. The trap frame lives near the top of that range.

The process-mapped kernel stack is not a user stack. It is mapped with kernel
permissions and exists so the kernel can find the saved trap frame through the
process's TTBR0 page table during `fork` and `exec`.

The design favors a fixed stack location because it makes trap-frame discovery
simple. The cost is a fixed stack size and less flexibility than dynamically
allocated per-thread kernel-stack VM regions.

## User Process Entry State

The first process setup path starts through the userspace bootstrap. The frame
is initialized with:

- user stack top at `USER_STACK_TOP`.
- entry point `USER_THREAD_START`.
- register `x0` containing a function pointer.
- register `x1` containing that function's argument.

For normal ELF programs after `exec`, userspace starts at the ELF header's
entry point. The user bootstrap receives `argc` and `argv`, initializes the
userspace heap, calls `main(argc, argv)`, and exits with `main`'s return value.

This two-step model exists because the original process path supported
function-entry processes through `spawn`, while the mature path executes ELF
programs from `/bin`.

## Process Address-Space Ownership

Each process owns one user page table selected through TTBR0. The kernel half
of the virtual address space is globally mapped through TTBR1, while the user
half changes when the scheduler switches threads.

The PCB stores:

- `ttbr0_el1`: physical address to load into TTBR0.
- `ttbr0_el1_va`: kernel-virtual pointer used by kernel page-table code.

This avoids repeatedly converting between physical and direct-mapped virtual
addresses in lifecycle paths.

User heap, stack, and ELF segments are part of the user page table. They are
populated lazily by the memory subsystem when faults occur. The process layer
does not manually load every page during creation.

## Page Table Lifetime

### Creation

`proc_create()` calls `initialize_user_page_table()`, stores the resulting root,
and then maps the kernel-stack/trap-frame region needed for user entry.

### Copy

`fork()` creates a child process and then copies the parent's page-table
metadata. User pages are shared with Copy-on-Write where possible:

- writable user PTEs are made read-only and marked CoW.
- the child receives read-only mappings to the same physical pages.
- physical page refcounts are incremented.
- a later write fault allocates and copies only the page that is actually
  modified.

This makes `fork` cheap for shell command execution, where the common pattern is
immediately `fork` then `exec`. The tradeoff is more complex page-fault and
refcounting behavior, which is why the details live in `memory.md`.

### Replacement

`exec` builds a new user page table, maps a fresh kernel-stack/trap-frame
region, builds the new user stack, registers lazy ELF segments, and then points
the PCB and executing thread at the new table. Depending on the caller path, it
may also install the new TTBR0 and destroy the old table immediately.

The important ordering decision is that the old address space remains intact
until the new executable has been validated and the replacement state is ready.
That gives `exec` normal failure semantics: on error, the old process image can
keep running.

### Destruction

`proc_destroy()` destroys the process page table, which walks page-table levels
and decrements page refcounts. It also removes the page-table metadata used by
lazy segment loading.

## Initial Process Bootstrap

`processes_init()` creates PID 0 with `proc_create()`, using the generated
`USER_INIT_PROCESS_ENTRY` symbol as a temporary entry. It then attempts to
replace that initial image with `/bin/init` through `k_exec_process()`.

The kernel stores userspace command ELFs in the filesystem under `/bin` before
the scheduler starts. That lets init and later shell commands follow the same
ELF loading path. If `/bin/init` fails to exec, the kernel logs an error and
keeps the process name as `init`, but the normal design expects `/bin/init` to
be available after filesystem seeding.

## Process Creation

`proc_create(func, args, ppid)` is the shared low-level creator used by init,
`spawn`, and `fork`.

It performs these steps:

1. Find an unused PCB slot.
2. Set parent, process group, current working directory, and child linkage.
3. Initialize children, thread list, file-descriptor vector, signal state, and
   child wait queue.
4. Create an empty user page table.
5. Create the main thread.
6. Map the kernel-stack/trap-frame region and initialize first-run state.
7. Add the process to its process group.

Parentless processes use their own PID as PGID and start in the root directory.
Children inherit their parent's PGID and cwd. File descriptors are not copied by
plain `proc_create`; `fork` performs descriptor inheritance after it creates the
child.

The separation is useful because `spawn` and `fork` need different resource
inheritance behavior even though both need a PCB, page table, and main thread.

## Fork Semantics

`fork` duplicates the current process from a syscall trap frame:

- creates a child PCB and main thread.
- copies the user address space with CoW.
- copies the parent's trap frame into the child's mapped kernel stack.
- sets the child's saved `x0` to `0`.
- copies the parent's file-descriptor vector.
- increments open-file references for inherited descriptors.
- enqueues the child thread.
- returns the child PID to the parent.

This matches the Unix contract that parent and child resume at the same
instruction with different return values. It also supports shell pipelines and
redirection because descriptors are inherited and can then be changed with
`dup2` before `exec`.

The major tradeoff is CoW complexity. A naive eager copy would be simpler, but
would waste memory and time for the dominant `fork`-then-`exec` workflow. CoW
keeps that workflow fast while preserving the illusion of independent address
spaces.

## Exec State Replacement

`exec(path, argv)` replaces the current process image with an executable ELF
loaded from the filesystem.

Preserved state:

- PID, PPID, PGID, and parent-child relationships.
- cwd.
- open file descriptors.
- ignored signal dispositions, except for `SIGKILL` and `SIGSTOP`.

Replaced state:

- user page table and memory segments.
- user stack contents.
- trap-frame entry point and argument registers.
- process name, derived from the executable path.
- caught signal handlers, reset to defaults.
- sibling threads, which are detached and terminated.

Failure state:

- if validation, open, permission checks, ELF parsing, stack setup, or segment
  registration fails, `exec` returns an error and does not commit the new image.

This preserves the key Unix behavior that successful `exec` does not return,
while failed `exec` leaves the caller alive to report the error.

## Argument Vector Handoff

The ELF exec path copies `argv` strings into kernel memory first. It then lays
out strings and the `argv` pointer array on the new user stack, aligned to 16
bytes.

On entry to the new program:

- `x0 = argc`
- `x1 = argv`
- `sp = user stack pointer`
- `elr = ELF entry point`

The userspace `_start` stub preserves those values, initializes the heap, calls
`main(argc, argv)`, and then calls `exit`.

The tradeoff is that argument copying has fixed limits (`EXEC_MAX_ARGS` and the
fixed user stack). That is much simpler than dynamically growing argv storage,
and adequate for the current command-line environment.

## Exit Semantics

`exit(code)` records the process exit code and terminates all threads in the
process. The current thread then yields and never returns to userspace.

When thread-state counters show that no live work remains, the PCB becomes a
zombie and `SIGCHLD` is sent to the parent. The process's resources are not
fully destroyed at this point because the parent may still need to collect the
status with `waitpid`.

This mirrors Unix's zombie state: the process is dead, but its parent-visible
status remains.

## Wait and Child Reaping

`waitpid(pid, status, flags)` supports:

- `pid == -1` to wait for any child.
- `pid < -1` to wait for a child in process group `-pid`.
- `pid > 0` to wait for one child PID.
- `WNOHANG` to return immediately when no matching child is waitable.
- `WUNTRACED` and `WCONTINUED` to report stopped and continued children.

If no matching child exists, the syscall returns `ECHILD`/`SYS_ECHILD`
depending on the path. If a matching child exists but is not yet waitable, the
calling thread records the requested PID and flags, enters the process child
wait queue, blocks interruptibly, and retries when woken.

When a zombie child is collected, `waitpid` returns the child PID, stores
`WAIT_EXITED` or `WAIT_SIGNALED` in `*status`, and calls `proc_destroy()` to
release resources. Stopped and continued children can be reported without
destroying the PCB.

The design uses one child wait queue per parent process rather than a global
wait table. That makes common parent-child wakeups direct and keeps wait state
near the relationship it describes.

## Parent-Child Relationships and Orphans

Children are stored as PID values in the parent's `children` vector. Process
creation appends the child PID to the parent; process destruction removes it.

When a process is destroyed, its children are handled in two ways:

- zombie children are recursively destroyed.
- live children are reparented to init, PID 0.

If live children are adopted, init's main thread is woken with
`BLOCK_UNTIL_NEW_CHILD`. This lets init serve the traditional Unix role of
orphan adopter and child reaper.

This is intentionally simpler than full session management and subreapers, but
it preserves the most important invariant: every live process has a parent that
can reap it.

## Process Groups and Job Control

Each process has a process group ID. New child processes inherit the parent's
PGID by default. `setpgid(pid, pgid)` can move a process into a different
group; `pgid == 0` means "use the target process's PID." Process groups are
tracked in a hashmap from PGID to a vector of member PIDs.

The shell uses process groups to implement foreground and background jobs:

- pipelines can be grouped together.
- signals can be sent to a whole group by passing a negative PID to `kill`.
- `tcsetpgrp` assigns a terminal foreground process group.
- terminal drivers can stop background jobs that violate TTY rules.

This is a pragmatic subset of POSIX job control. The OS implements the process
group mechanics needed for an interactive shell without modeling every session,
controlling-terminal, or permission rule found in a production Unix kernel.

## Per-Process Resource Ownership

### File Descriptor Table

Each process owns a vector mapping userspace file descriptors to kernel
open-file table descriptors. The OFT entry owns the underlying inode/device/pipe
state and cursor metadata.

`fork` copies descriptor numbers and increments open-file references, so parent
and child share open file descriptions as Unix programs expect. `dup2` closes
the target descriptor, points it at the old descriptor's OFT entry, and
increments that reference.

On process destruction, every valid file descriptor is closed through `k_close`.
This pushes cleanup through the filesystem and VFS file-operation path, so pipe,
device, and regular-file cleanup stay with their owning subsystem.

### Current Working Directory

The PCB stores `cwd` as an inode ID. Children inherit the parent's cwd at
creation. Filesystem helpers such as `cd`, `ls` with no path, relative lookup,
and `getcwd` use this field.

Storing an inode ID instead of a path string keeps cwd lookup compact and avoids
duplicating path text in every process. The tradeoff is that user-facing path
reconstruction has to ask the filesystem to map inode relationships back to a
string.

## Blocking and Wakeup Paths

Thread blocking is represented directly in the TCB state. The kernel currently
uses three broad blocking modes:

- interruptible waits, used for `waitpid` and signal-aware waits.
- killable waits, where termination signals can break the wait.
- uninterruptible waits, where only the requested event wakes the thread.

The `blocked_until` event bitmask records event-based waits. Public event bits
include:

| Event bit | Meaning |
|---|---|
| `BLOCK_UNTIL_NEW_CHILD` | Wake when child/adoption state changes. |
| `BLOCK_UNTIL_SIGNAL` | Wake for signal delivery. |
| `BLOCK_UNTIL_TIMER` | Wake for timer expiration. |
| `BLOCK_UNTIL_TTY_REQUEST` | Wake when a TTY shell request is pending. |

Subsystems that own specific wait queues, such as UART RX, pipes, semaphores,
mutexes, condition variables, and child waits, keep their own waiter vectors and
move threads back to `THREAD_READY` when work becomes available.

The implementation favors simple vectors over a generic wait-channel subsystem.
This is easy to debug and sufficient for single-core execution, but it does not
provide sophisticated cancellation, priority inheritance, timeout composition,
or lock-free wakeup ordering.

## Signals

Signals are split between process-level state and thread-level state:

- the PCB owns the signal action table and process-pending signal bitset.
- each TCB owns a pending signal bitset and current signal mask.

Default actions are initialized by the signal subsystem. The documented signal
set includes termination signals (`SIGINT`, `SIGKILL`, `SIGTERM`), stop signals
(`SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`), continue (`SIGCONT`), and
parent notification (`SIGCHLD`).

Delivery is checked during scheduler transitions. Process-pending signals are
delivered to the current thread when unmasked; thread-pending signals are then
checked separately. Default stop and kill behavior can directly stop or
terminate the current thread. User-defined handlers are entered through the
signal handler path described in the signal API docs.

This checkpoint model is simpler than asynchronous signal injection at every
instruction boundary. It fits the current kernel because all user/kernel
transitions already pass through the scheduler/trap machinery, but signal
latency depends on scheduling and trap return opportunities.

The full signal path is documented in [signals.md](signals.md). This process
document only describes the scheduler and lifecycle integration points.

## Sleep Integration

`sleep(ms)` is a scheduler-blocking operation backed by the software timer
table, not a busy loop. The syscall path calls `timer_sleep_ms(ms)`.

The process-facing flow is:

1. Resolve the current TCB.
2. If `ms == 0`, yield once and return.
3. Set `BLOCK_UNTIL_TIMER` in `tcb->blocked_until`.
4. Register a software timer callback for the current TID.
5. Mark the thread `THREAD_BLOCKED_INTERRUPTABLE`.
6. Yield to the scheduler.
7. When the timer IRQ fires, the timer callback wakes the TCB or clears the
   timer bit if the thread is currently stopped over a blocked state.

This keeps sleeping threads off the run queues until their timer expires. The
timer subsystem owns deadlines and hardware rearming; the process subsystem
only owns the blocked thread state and scheduler handoff.

## Multithreading and Synchronization

The kernel has TCB-level support for multiple threads per process and provides
basic synchronization primitives:

- mutexes with owner TID, wait vector, and lock tracking.
- semaphores with a count and waiter vector.
- condition variables associated with a mutex.
- `/proc/locks` formatting for tracked mutexes and semaphores.

These primitives are currently kernel-facing infrastructure. They block by
marking threads stopped or blocked and yielding to the scheduler, then waking
one or more waiting TIDs later.

The design is intentionally small because the OS currently runs on one core.
It demonstrates the shape of synchronization primitives without needing atomic
SMP algorithms, priority inheritance, robust mutex ownership recovery, or
userspace futexes.

## System Call Integration

Process-related syscalls are:

| SVC | Wrapper | Kernel behavior |
|---:|---|---|
| 4 | raw `S_YIELD` | Voluntarily run the scheduler. |
| 5 | `exit` | Terminate current process. |
| 6 | `getpid` | Return current PID. |
| 9 | `spawn` | Create child process at a userspace function pointer. |
| 10 | `waitpid` | Wait for child status changes and reap zombies. |
| 12 | `kill` | Send signal to process or process group. |
| 13 | `block_until_event` | Block on event bits. |
| 34 | `fork` | Duplicate current process with CoW. |
| 35 | `dup2` | Duplicate file descriptors for redirection/pipes. |
| 36 | `setpgid` | Change process group membership. |
| 37 | `getpgrp` | Return current process group. |
| 38 | `tcsetpgrp` | Change terminal foreground process group. |
| 42 | `ps` | Print process information. |
| 43 | `exec` | Replace process image with ELF from filesystem. |
| 45 | `sleep` | Block current thread until timer wakeup. |
| 54 | `proc_change_priority` | Change scheduler priority for a process. |

Signals also expose their own syscall family, documented in
[signals-api.md](../api-docs/signals-api.md).

## Subsystem Integration Boundaries

### Memory Management Boundary

The process layer asks memory management to create, copy, replace, and destroy
page tables. It relies on the memory subsystem for CoW write faults, lazy
segment loading, heap/stack page allocation, TLB invalidation, and `/proc` VM
formatting.

### Filesystem Boundary

The filesystem owns path lookup, file permissions, OFT entries, `k_open`,
`k_close`, ELF reads, `/bin` seeding, and `/proc` generation. Process code only
stores descriptor mappings and cwd inode IDs.

### Trap Boundary

The trap layer owns exception entry/exit and raw register save/restore. Process
code mutates trap frames only when implementing lifecycle semantics such as
`fork` return values and `exec` entry state.

### Scheduler Boundary

The scheduler owns ready queues and dispatch policy. Process/thread helpers own
state transitions and notify the scheduler when a TCB becomes runnable.

### Signal Boundary

Signal APIs own validation, default dispositions, masks, pending bitsets,
process-group delivery, and handler setup. The scheduler checks pending signals
at delivery checkpoints, and thread helpers apply stop/continue/terminate
effects. See [signals.md](signals.md) for the complete delivery path.

### Userspace Boundary

Userspace owns command behavior, shell parsing, job table policy, and user
library wrappers. The kernel exposes primitives instead of embedding shell
semantics in scheduler code.

## Error Handling and Cleanup Policy

The process subsystem mostly returns negative `SYS_E*` values or filesystem
error values through syscall paths. Cleanup is staged:

- failed `exec` validates the new image before committing it.
- successful `exec` replaces the page table only after stack and segment setup.
- `exit` creates a zombie before final destruction so parent status is
  observable.
- `waitpid` performs final process destruction only when reaping a zombie.
- `proc_destroy` closes descriptors through filesystem APIs and destroys the
  page table through memory APIs.

One current limitation is partial allocation cleanup in some failure paths.
For example, if `proc_create()` fails after allocating early vectors or a user
page table, the code does not always unwind every intermediate allocation before
returning an error. That is acceptable for the current educational scope but
should be tightened if the kernel is expected to run long stress workloads.

## Testing and Debugging Hooks

Process state is observable through:

- `ps`, which prints process information from the process table.
- `/proc/processes`, one row per live process.
- `/proc/<pid>/status`, including IDs, state, thread counters, cwd inode,
  open-file count, pending signals, blocked-event mask, and TTBR0.
- `/proc/<pid>/fd`, showing descriptor mappings.
- `/proc/<pid>/threads`, showing per-process thread state.
- `/proc/threads`, one row per live thread.
- `/proc/locks`, tracked mutex and semaphore state.
- `/proc/syscalls`, syscall invocation counts.

Userspace smoke tests include process/orphan scheduling tests and
`waitpid`/signal tests. These are useful for demonstrations, but they are not a
substitute for automated regression tests around fork/exec/wait failure paths.

## Design Tradeoffs and Limits

- Single-core scheduling keeps the scheduler global and understandable, but it
  does not exercise SMP locking, per-CPU run queues, or cross-core wakeups.
- Fixed PID slots make lookup and `/proc` simple, but impose a hard process
  limit and do not protect against PID reuse races.
- Thread scheduling with process-owned resources matches Unix structure, but
  makes `exec`, `exit`, and signal delivery more complex than a purely
  single-threaded process model.
- Copy-on-Write makes `fork` efficient for shell workloads, but shifts
  complexity into page-table copying, refcounts, TLB invalidation, and fault
  handling.
- Event-bit blocking is compact and easy to trace, but less expressive than
  generic wait channels with deadlines and cancellation.
- Process groups support practical shell job control, but sessions and full
  POSIX controlling-terminal rules are intentionally simplified.
- Synchronization primitives demonstrate kernel wait queues, but they are not
  designed as production SMP primitives.
- `spawn` remains available for function-entry processes even though normal
  userspace execution now uses ELF `exec`; this preserves a useful test path at
  the cost of having two process-entry models.
