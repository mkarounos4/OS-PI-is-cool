# Signals API Reference

Userspace enters signal APIs through `svc #0` wrappers in `user/lib/signals.h`
and `user/lib/syscall.c`. Signal sets are 32-bit integer bitsets; bit `N`
represents signal number `N`. Return values are `0` on success unless noted, or
a negative `SYS_E*` kernel error value.

For the kernel delivery path, scheduler checkpoints, process-group behavior,
and `SIGCHLD`/`waitpid` integration, see
[Signal Handling Architecture](../architecture/signals.md).

## Signal Syscall Table

| SVC | Public enum | Userspace API | Kernel entry | Summary |
|---:|---|---|---|---|
| 12 | `S_KILL` | `kill` | `s_kill` | Send a signal to a process or process group. |
| 28 | `S_SIGPROCMASK` | `sigprocmask` | `sigprocmask` | Read or change the current thread signal mask. |
| 29 | `S_SIGEMPTYSET` | `sigemptyset` | `sigemptyset` | Initialize a signal set to empty. |
| 30 | `S_SIGADDSET` | `sigaddset` | `sigaddset` | Add one signal number to a signal set. |
| 31 | `S_SIGFILLSET` | `sigfillset` | `sigfillset` | Initialize a signal set with all 32 signal bits set. |
| 32 | `S_SIGSUSPEND` | `sigsuspend` | `sigsuspend` | Temporarily install a mask and block until a signal. |
| 33 | `S_SIGACTION` | `sigaction` | `sigaction` | Install a signal handler and optionally read the previous action. |

## Userspace Signal Table

| Header | Name | Value / SVC | Summary |
|---|---|---:|---|
| `lib/syscall.h` | `kill(pid, signal)` | 12 | Send `signal` to `pid` or process group `-pid`. |
| `lib/signals.h` | `sigprocmask(how, set, oldset)` | 28 | Block, unblock, or replace the current thread mask. |
| `lib/signals.h` | `sigemptyset(set)` | 29 | Store an empty signal set in `*set`. |
| `lib/signals.h` | `sigaddset(set, signum)` | 30 | Set the bit for `signum` in `*set`. |
| `lib/signals.h` | `sigfillset(set)` | 31 | Store `0xffffffff` in `*set`. |
| `lib/signals.h` | `sigsuspend(mask)` | 32 | Wait with `mask` installed until interrupted by a signal. |
| `lib/signals.h` | `sigaction(signum, sa, old)` | 33 | Install `sa` for `signum`; copy old action to `old` when non-NULL. |
| `lib/signals.h` | `SIG_DFL` | `0` | Request default handling in `sigaction`. |
| `lib/signals.h` | `SIG_IGN` | `1` | Request ignored handling in `sigaction`. |
| `lib/signals.h` | `SIG_BLOCK` | `0` | Add signals in `set` to the current mask. |
| `lib/signals.h` | `SIG_UNBLOCK` | `1` | Remove signals in `set` from the current mask. |
| `lib/signals.h` | `SIG_SETMASK` / `SIG_SET` | `2` | Replace the current mask with `set`. |

## Signal Numbers

| Signal | Number | Default action | Notes |
|---|---:|---|---|
| `SIGINT` | 2 | Terminate | Sent by terminal interrupt handling. |
| `SIGTTOU` | 6 | Stop | Used by job-control terminal output handling. |
| `SIGTTIN` | 7 | Stop | Used when background jobs read from the foreground TTY. |
| `SIGTSTP` | 8 | Stop | Sent by terminal suspend handling. |
| `SIGKILL` | 9 | Terminate | Cannot be caught or ignored. |
| `SIGSTOP` | 10 | Stop | Cannot be caught or ignored. |
| `SIGCONT` | 11 | Continue | Continues stopped threads. |
| `SIGCHLD` | 12 | Ignore | Used to notify parents about child state changes. |
| `SIGTERM` | 15 | Terminate | General termination signal. |

## Data Types

### `sigset_t`

`sigset_t` is an `int` bitset. Signal `N` is represented by `(1 << N)`.
Only signal numbers `0` through `31` are accepted by the kernel helpers.

### `struct sigaction`

```c
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};
```

`sa_handler` may be `SIG_DFL`, `SIG_IGN`, or a userspace function pointer with
type `void (*)(int)`. While a userspace handler runs, the kernel blocks the
delivered signal and every signal in `sa_mask`; it restores the old mask after
the handler returns. `sa_flags` is stored but no flag behavior is currently
documented by the implementation.

## Mini Reference

### `kill(pid_t pid, int signal)` - SVC 12

Sends `signal` to one process when `pid > 0`. When `pid < 0`, sends to every
member of process group `-pid`. Returns `0` when at least one target accepts the
signal, `SYS_ESRCH` when no target exists, or `SYS_EINVAL` for an invalid signal
number.

Default stop, continue, and terminate signals are delivered to all threads in a
target process. Other non-ignored signals are delivered to one unmasked thread,
or stored as process-pending if all threads currently mask the signal.

### `sigprocmask(int how, const sigset_t *set, sigset_t *oldset)` - SVC 28

Changes the current thread signal mask. If `oldset` is non-NULL, the previous
mask is copied there before applying the change. `how` must be `SIG_BLOCK`,
`SIG_UNBLOCK`, or `SIG_SETMASK` / `SIG_SET`. Returns `SYS_EFAULT` for a NULL
`set`, `SYS_ESRCH` when there is no current thread, or `SYS_EINVAL` for an
unknown `how`.

### `sigemptyset(sigset_t *set)` - SVC 29

Stores `0` into `*set`. Returns `SYS_EFAULT` if `set` is NULL.

### `sigaddset(sigset_t *set, int signum)` - SVC 30

Adds `signum` to `*set` by setting bit `signum`. Returns `SYS_EFAULT` if `set`
is NULL or `SYS_EINVAL` if `signum` is outside `0..31`.

### `sigfillset(sigset_t *set)` - SVC 31

Stores `0xffffffff` into `*set`, blocking or selecting all 32 signal slots when
used as a mask. Returns `SYS_EFAULT` if `set` is NULL.

### `sigsuspend(const sigset_t *mask)` - SVC 32

Temporarily replaces the current thread mask with `*mask`, blocks the thread
until signal delivery wakes it, restores the previous mask, and returns
`SYS_EINTR`. Returns `SYS_EFAULT` for a NULL mask or `SYS_ESRCH` when there is
no current thread.

### `sigaction(int signum, struct sigaction *sa, struct sigaction *old)` - SVC 33

Installs `*sa` as the action for `signum`. If `old` is non-NULL, the previous
action is copied to `*old`. `SIG_DFL` and `SIG_IGN` userspace sentinel values
are translated to the kernel default and ignore handlers. Returns `SYS_ESRCH`
when there is no current process, `SYS_EINVAL` for an invalid signal number, or
`SYS_EFAULT` if `sa` is NULL.

`SIGKILL` and `SIGSTOP` may only use the default action; attempts to catch or
ignore them return `SYS_EINVAL`.

## Delivery Notes

Default actions are initialized by `initialize_signals()`. `SIGINT`, `SIGKILL`,
and `SIGTERM` terminate; `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, and `SIGTTOU` stop;
`SIGCONT` continues; `SIGCHLD` is ignored.

Signals can interrupt `THREAD_BLOCKED_INTERRUPTABLE` waits. Terminating signals
also interrupt `THREAD_BLOCKED_KILLABLE` waits. `SIGCONT` clears pending default
stop signals for the target process before continuing stopped threads.
