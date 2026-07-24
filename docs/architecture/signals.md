# Signal Handling Architecture

This document describes how signals move through userspace, syscalls, process
state, thread state, scheduler checkpoints, terminal job control, and
`waitpid`. The public constants and function signatures are documented in
[signals-api.md](../api-docs/signals-api.md); this file focuses on the kernel
path and the design decisions behind it.

## List of Features

- [System structure](#system-structure)
- [Signal model](#signal-model)
- [Signal numbers and defaults](#signal-numbers-and-defaults)
- [Userspace entry points](#userspace-entry-points)
- [Kernel signal state](#kernel-signal-state)
- [Initialization](#initialization)
- [Sending a signal](#sending-a-signal)
- [Process-group delivery](#process-group-delivery)
- [Thread-directed delivery](#thread-directed-delivery)
- [Masks and pending signals](#masks-and-pending-signals)
- [Scheduler delivery checkpoints](#scheduler-delivery-checkpoints)
- [Default actions](#default-actions)
- [User-defined handlers](#user-defined-handlers)
- [Sigsuspend](#sigsuspend)
- [SIGCHLD and waitpid](#sigchld-and-waitpid)
- [Terminal job-control signals](#terminal-job-control-signals)
- [Exec and signal dispositions](#exec-and-signal-dispositions)
- [Blocking interaction](#blocking-interaction)
- [Error handling](#error-handling)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)

## System Structure

```
+--------------------------------------------------+
| userspace kill/sigaction/sigprocmask/sigsuspend  |
+--------------------------------------------------+
| svc #0 syscall dispatcher                         |
+--------------------------------------------------+
| kernel/signals: validation, pending bits, masks   |
+--------------------------------------------------+
| process table: actions, process-pending signals   |
+--------------------------------------------------+
| thread table: thread-pending signals, masks       |
+--------------------------------------------------+
| scheduler checkpoints and thread state changes    |
+--------------------------------------------------+
| waitpid, process groups, TTY foreground control   |
+--------------------------------------------------+
```

# Detailed Architecture and Decisions

## Signal Model

Signals are a small POSIX-inspired notification mechanism. A signal can:

- be ignored.
- terminate a thread/process.
- stop a thread/process.
- continue a stopped thread/process.
- interrupt an interruptible wait.
- run a user-installed handler.
- notify a parent that a child changed state.

The implementation separates process-wide state from thread-local delivery:

- the process owns the action table and process-pending bitset.
- each thread owns its signal mask and thread-pending bitset.

This matches the Unix idea that signal dispositions belong to a process, while
delivery is ultimately performed against a thread that can run or be woken.

## Signal Numbers and Defaults

The supported signal numbers are:

| Signal | Number | Default behavior |
|---|---:|---|
| `SIGINT` | 2 | Terminate. |
| `SIGTTOU` | 6 | Stop. |
| `SIGTTIN` | 7 | Stop. |
| `SIGTSTP` | 8 | Stop. |
| `SIGKILL` | 9 | Terminate and cannot be caught or ignored. |
| `SIGSTOP` | 10 | Stop and cannot be caught or ignored. |
| `SIGCONT` | 11 | Continue. |
| `SIGCHLD` | 12 | Ignore by default, but also wakes matching `waitpid` callers. |
| `SIGTERM` | 15 | Terminate. |

All other signal slots in the 0..31 range are initialized to a
not-implemented default handler. Keeping the signal set to 32 bits makes masks
cheap: `signalset_t` is an integer bitset, and bit `N` represents signal `N`.

## Userspace Entry Points

Userspace reaches signals through normal syscalls:

| Syscall | Purpose |
|---|---|
| `kill(pid, signal)` | Send a signal to one process or a process group. |
| `sigprocmask(how, set, oldset)` | Read or change the current thread mask. |
| `sigemptyset(set)` | Clear a signal set. |
| `sigaddset(set, signum)` | Add one signal to a set. |
| `sigfillset(set)` | Fill all 32 bits. |
| `sigsuspend(mask)` | Temporarily install a mask and sleep until a signal. |
| `sigaction(signum, sa, old)` | Install or read a process signal action. |

All of these enter the kernel through `svc #0`. The syscall dispatcher routes
SVC numbers 12 and 28 through 33 into `kernel/signals/signals.c`.

## Kernel Signal State

The PCB stores:

- `sigactions[32]`: process-wide dispositions.
- `pending_signals`: process-pending bitset.
- `wait_stop_pending` and `wait_cont_pending`: child-state markers consumed by
  `waitpid(WUNTRACED)` and `waitpid(WCONTINUED)`.
- `child_waitq`: parent threads blocked in `waitpid`.

The TCB stores:

- `pending_signals`: thread-pending bitset.
- `mask`: thread-local blocked-signal mask.
- `blocked_until`: event bits such as `BLOCK_UNTIL_SIGNAL`.

The design uses process-level dispositions because `exec`, `sigaction`, and
ignore/default behavior are process properties. It uses thread-level masks
because one thread may be able to accept a signal while another masks it.

## Initialization

`initialize_signals()` runs during kernel initialization before
`scheduler_init()`. It first assigns every default handler slot to
`SIG_NOT_IMPLEMENTED`, then installs the implemented defaults:

- `SIGCONT` maps to continue.
- `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, and `SIGTTOU` map to stop.
- `SIGINT`, `SIGKILL`, and `SIGTERM` map to terminate.
- `SIGCHLD` maps to ignore.

Every new process initializes its `sigactions` entries to `SIG_DFL` with empty
masks and zero flags. This means the default handler table, not each PCB,
defines the actual default action.

## Sending a Signal

`s_kill(pid, signal)` is the main kernel entry point for process-directed
signals.

The flow is:

1. Validate `signal` is in `0..31`.
2. If `pid < 0`, treat `-pid` as a process group and broadcast to members.
3. Look up the target PCB.
4. Resolve the process disposition:
   - if the action is `SIG_DFL`, use the default handler table.
   - otherwise use the installed handler.
5. If the effective behavior is stop, continue, or terminate, send the signal
   to all threads with `pthread_kill`.
6. If the effective behavior is ignore, return success.
7. Otherwise, pick the first thread that does not mask the signal, mark it
   thread-pending, and handle any interruptible state immediately.
8. If every thread masks the signal, mark it process-pending.

The "all threads for stop/continue/terminate" rule is important. A stopped or
terminated process should not keep hidden sibling threads runnable just because
only one thread received the signal.

## Process-Group Delivery

Negative `pid` values target process groups. `s_kill(-pgid, signal)` looks up
the PGID in the process-group hashmap and recursively sends the same signal to
each member PID.

This is the path used by shell job control:

- foreground jobs can receive terminal-generated stop or interrupt signals.
- background or pipeline jobs can be killed or continued as a group.
- the shell can use one API shape for both a single process and a job.

The tradeoff is that process groups are a lightweight subset. The OS does not
currently model full POSIX sessions or all controlling-terminal permission
rules.

## Thread-Directed Delivery

`pthread_kill(tid, signal)` targets a specific TCB:

1. Resolve the TID.
2. For `SIGCONT`, clear process-pending stop signals that would conflict with
   continuing the process.
3. Add the signal to the thread-pending bitset.
4. If the thread does not mask the signal, try to wake it from
   `BLOCK_UNTIL_SIGNAL`.
5. If the thread is interruptibly blocked, killably blocked, or stopped, apply
   immediate stop/continue/terminate effects through
   `handle_interruptable_signal`.

This lets signals wake blocked threads without waiting for the next normal
timeslice, but it still preserves mask behavior for ordinary pending delivery.

## Masks and Pending Signals

`sigprocmask` acts on the current TCB:

- `SIG_BLOCK` ORs bits into `tcb->mask`.
- `SIG_UNBLOCK` clears bits from `tcb->mask`.
- `SIG_SETMASK` replaces the mask.
- `oldset`, when non-NULL, receives the previous mask.

If a process-directed signal cannot find an unmasked thread, it is stored in
the PCB's `pending_signals`. If a thread-directed signal is sent to a masked
thread, it remains in the TCB's `pending_signals`.

Pending delivery is retried at scheduler checkpoints. This makes the mask
model simple, but it also means unmasking a signal does not immediately run its
handler in the middle of `sigprocmask`; delivery waits until the scheduler path
checks pending signals.

## Scheduler Delivery Checkpoints

`scheduler_tick()` calls `scheduler_handle_pending_signals()` after a context
switch returns to that scheduler invocation. The handler checks:

1. process-pending signals for the current process.
2. thread-pending signals for the current thread.

For each pending unmasked signal:

- `SIGKILL` and default `SIGSTOP` are special-cased first because they change
  thread state immediately.
- `SIG_DFL` and `SIG_IGN` call the kernel default/ignore handlers.
- user-installed handlers call `user_def_sig_handler`.

The signal checkpoint sits in scheduler code because delivery can affect which
thread should run next. Stop and terminate handlers can yield away from the
current thread, while continue can move a stopped thread back toward runnable
state.

## Default Actions

Default actions are implemented as kernel functions:

- `SIG_IGN` does nothing.
- `SIG_CONT` calls `continue_thread` for the current thread.
- `SIG_STOP` calls `stop_thread` for the current thread.
- `SIG_TERM` calls `terminate_thread` for the current thread.
- `SIG_DFL` indexes into the default handler table.

Thread state helpers notify the TTY session layer and update process-level
thread counters. If a process becomes stopped, running, or zombie, the parent
can be notified through `SIGCHLD`.

## User-Defined Handlers

When a signal has a userspace handler, the scheduler calls
`user_def_sig_handler(signum)`. That helper:

1. Builds a temporary mask from the action's `sa_mask` plus the delivered
   signal bit.
2. Blocks that mask while saving the old mask.
3. Calls the handler function pointer.
4. Restores the old mask.

This is a direct-call model. The current implementation does not build a full
userspace signal frame/trampoline that interrupts arbitrary EL0 execution and
later returns through a dedicated `sigreturn` syscall. The simpler approach is
enough for the current user programs, but it is not a production Unix signal
ABI.

## Sigsuspend

`sigsuspend(mask)` temporarily replaces the current thread mask, marks the
thread blocked on `BLOCK_UNTIL_SIGNAL`, and blocks interruptibly. When a signal
wakes the thread, the old mask is restored and the syscall returns `SYS_EINTR`.

This follows the usual signal-waiting pattern: change the mask and sleep as one
kernel operation so a signal cannot be missed between separate userspace calls.

## SIGCHLD and Waitpid

`SIGCHLD` has two related jobs:

- default signal disposition is ignore.
- child state changes wake parent threads blocked in `waitpid`.

When a thread state change changes the aggregate process state,
`pcb_thread_change_state()` calls `send_sigchld(child_pid)` for running,
stopped, and zombie transitions. `send_sigchld`:

1. Looks up the child PCB.
2. Marks `wait_stop_pending` for stopped children.
3. Marks `wait_cont_pending` for continued/running children.
4. Finds the parent PCB.
5. Scans the parent's `child_waitq`.
6. Wakes a waiting thread if its requested PID or process group matches and
   its wait flags permit the observed state.

This keeps `waitpid` and signals coupled in the same way Unix does: parent
notification is a signal concept, but reaping remains a wait concept.

## Terminal Job-Control Signals

TTY and shell job control use signals to enforce foreground/background rules:

- `SIGINT` can terminate foreground jobs.
- `SIGTSTP` stops foreground jobs.
- `SIGTTIN` stops background jobs that try to read from the foreground TTY.
- `SIGTTOU` stops background jobs that try to write when terminal policy
  requires it.
- `SIGCONT` resumes stopped jobs.

The terminal layer owns foreground process group state. The signal layer only
delivers requested signals to PIDs or process groups and applies state changes.
This keeps terminal policy out of the scheduler.

## Exec and Signal Dispositions

Successful `exec` resets caught signal handlers to defaults, preserves ignored
dispositions, and always restores `SIGKILL` and `SIGSTOP` to default behavior.
It also clears handler masks and flags for reset entries.

This matches the practical Unix rule that ignored signals stay ignored across
`exec`, but old handler function pointers from the previous program image do
not remain valid after the address space is replaced.

## Blocking Interaction

Signals interact with blocked threads through `handle_interruptable_signal`:

- `SIGKILL` terminates regardless of normal mask/default handling once delivered
  to the thread.
- terminating signals kill interruptible and killable waits.
- stop signals stop interruptible waits.
- ordinary non-continue signals can unblock an interruptible wait.
- continue signals resume stopped threads.

Uninterruptible waits are not broken by ordinary signal delivery. That gives
device and kernel paths a way to wait for a must-complete event, although the
current kernel uses this sparingly.

## Error Handling

Signal syscalls follow errno-style failures:

- invalid signal numbers return `SYS_EINVAL`.
- missing PIDs or current process/thread state return `SYS_ESRCH`.
- NULL user pointers return `SYS_EFAULT`.
- attempts to catch or ignore `SIGKILL` or `SIGSTOP` return `SYS_EINVAL`.

The implementation stores signal sets and actions directly through pointers
passed by syscall wrappers. The surrounding syscall ABI reports negative
errno-style values to userspace.

## Design Tradeoffs and Limits

- Signal sets are fixed 32-bit bitsets, which keeps masks cheap but limits the
  signal namespace.
- Delivery is checkpoint-based in the scheduler rather than fully asynchronous
  at every user instruction.
- User handlers use a direct-call model rather than a full EL0 signal frame and
  `sigreturn` ABI.
- Process groups support shell job control, but full POSIX session semantics
  are intentionally out of scope.
- Pending signals are represented as bits, so repeated deliveries of the same
  signal coalesce instead of being queued with counts or payloads.
- The design favors clarity and enough Unix behavior for an educational shell
  over realtime signal semantics, alternate stacks, `SA_*` flag behavior, and
  SMP-safe delivery rules.
