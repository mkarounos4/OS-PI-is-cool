# API Reference

## Syscall Table

Userspace enters the kernel with `svc #0`. The syscall number is passed in
`x8`, arguments are passed in `x0` through `x5`, and the return value is
returned in `x0`. Negative return values are errno-style failures.

| SVC | Public enum | Userspace wrapper | Summary |
|---:|---|---|---|
| 1 | `S_WRITE_CONSOLE` | `write_console` | Write bytes directly to the console UART. |
| 2 | `S_PUTC` | `putc` | Write one character to the console UART. |
| 3 | `S_GET_TICKS` | `get_ticks` | Return the current timer tick counter. |
| 4 | `S_YIELD` | `sys_call0(S_YIELD)` | Voluntarily yield the CPU. |
| 5 | `S_EXIT` | `exit` | Terminate the current process. |
| 6 | `S_GETPID` | `getpid` | Return the current process id. |
| 7 | `S_CURRENT_EL` | raw syscall only | Reserved/current exception-level slot. |
| 8 | `S_DELAY` | `delay` | Busy/blocking millisecond delay. |
| 9 | `S_SPAWN` | `spawn` | Start a process from an in-memory function entry. |
| 10 | `S_WAITPID` | `waitpid` | Wait for child process state changes. |
| 11 | `S_SBRK` | `sbrk` | Validate a user heap break range. |
| 12 | `S_KILL` | `kill` | Send a signal to a process or process group. |
| 13 | `S_BLOCK_UNTIL_EVENT` | `block_until_event` | Block until one of the requested kernel events occurs. |
| 14 | `S_FS_TOUCH` | `touch` | Create files or update their metadata. |
| 15 | `S_FS_MV` | `mv` | Rename or move a filesystem path. |
| 16 | `S_FS_RM` | `rm` | Remove one or more paths. |
| 17 | `S_FS_CAT` | `cat` | Kernel-level cat/copy helper. |
| 18 | `S_FS_CP` | `cp` | Kernel-level copy helper. |
| 19 | `S_FS_CHMOD` | `fs_chmod` | Change file permissions. |
| 20 | `S_FS_LS` | `ls` | List a directory. |
| 21 | `S_FS_MKDIR` | `fs_mkdir` | Create directories. |
| 22 | `S_FS_CD` | `cd` | Change the current process working directory. |
| 23 | `S_FS_OPEN` | `open` | Open a file, directory, device, or pipe endpoint. |
| 24 | `S_FS_CLOSE` | `close` | Close a file descriptor. |
| 25 | `S_FS_LSEEK` | `lseek` | Reposition a seekable file descriptor. |
| 26 | `S_FS_READ` | `read` | Read bytes from a file descriptor. |
| 27 | `S_FS_WRITE` | `write` | Write bytes to a file descriptor. |
| 28 | `S_SIGPROCMASK` | `sigprocmask` | Change or read the process signal mask. |
| 29 | `S_SIGEMPTYSET` | `sigemptyset` | Initialize an empty signal set. |
| 30 | `S_SIGADDSET` | `sigaddset` | Add a signal to a signal set. |
| 31 | `S_SIGFILLSET` | `sigfillset` | Initialize a full signal set. |
| 32 | `S_SIGSUSPEND` | `sigsuspend` | Wait with a temporary signal mask. |
| 33 | `S_SIGACTION` | `sigaction` | Install or read a signal handler. |
| 34 | `S_FORK` | `fork` | Duplicate the current process. |
| 35 | `S_DUP2` | `dup2` | Duplicate one file descriptor onto another. |
| 36 | `S_SETPGID` | `setpgid` | Set a process group id. |
| 37 | `S_GETPGRP` | `getpgrp` | Return the current process group id. |
| 38 | `S_TCSETPGRP` | `tcsetpgrp` | Assign terminal foreground process group. |
| 39 | `S_FS_MOUNT` / kernel `S_MOUNT` | `mount` | Reserved; userspace wrapper currently returns success without trapping. |
| 40 | `S_FS_UNMOUNT` / kernel `S_UNMOUNT` | `unmount` | Reserved; userspace wrapper currently returns `-ENOSYS`. |
| 41 | `S_PIPE` | `pipe` | Create a pipe and return two descriptors. |
| 42 | `S_PS` | `ps` | Print process information. |
| 43 | `S_EXEC` | `exec` | Replace the current image with an ELF executable. |
| 44 | `S_GETCWD` | `getcwd` | Copy the current working directory into a buffer. |
| 45 | `S_SLEEP` | `sleep` | Sleep for a number of milliseconds. |
| 46 | `S_STAT` | `stat` | Read metadata for a filesystem path. |
| 47 | `S_TTY_NEXT_REQUEST` | `tty_next_request` | Pop a pending request for another shell TTY. |
| 48 | `S_TTY_GET_MODE` | `tty_get_mode` | Read terminal canonical/raw mode. |
| 49 | `S_TTY_SET_MODE` | `tty_set_mode` | Set terminal canonical/raw mode. |
| 50 | `S_TTY_GET_SIZE` | `tty_get_size` | Read terminal rows and columns. |
| 51 | `S_TTY_SCREEN_ENTER` | `tty_screen_enter` | Enter alternate screen mode. |
| 52 | `S_TTY_SCREEN_LEAVE` | `tty_screen_leave` | Leave alternate screen mode. |
| 53 | `S_TTY_SCREEN_PRESENT` | `tty_screen_present` | Present a complete text screen buffer. |
| 54 | `S_PROC_CHANGE_PRIORITY` | `proc_change_priority` | Change a process scheduler priority. |

## Syscall Notes

### `write_console(const char *s, uint64_t len)` - SVC 1

Writes up to the kernel console chunk limit from a userspace buffer. Returns the
number of bytes written or a negative errno such as `-EFAULT`.

### `putc(char c)` - SVC 2

Writes one character to the UART console. Returns `0` on success.

### `get_ticks(void)` - SVC 3

Returns the monotonic timer tick counter used by the kernel timer subsystem.

### `yield` - SVC 4

No named wrapper is provided. Use `sys_call0(S_YIELD)` to ask the scheduler to
switch away from the current thread.

### `exit(int code)` - SVC 5

Terminates the current process, stores the exit code, terminates sibling threads,
and wakes the scheduler. This call does not return.

### `getpid(void)` - SVC 6

Returns the current process id or `-ESRCH` if no current process is active.

### `current_el` - SVC 7

Reserved in the public syscall table. The dispatcher currently falls through to
the same delay handling as `S_DELAY`.

### `delay(uint64_t ms)` - SVC 8

Delays for approximately `ms` milliseconds using the kernel timer delay path.
This is intended for tests and simple blocking waits.

### `spawn(void *(*func)(void *), void *arg)` - SVC 9

Creates a child process that starts at a userspace function pointer with one
argument. Returns the child pid or a negative errno.

### `waitpid(pid_t pid, int *status, uint32_t flags)` - SVC 10

Waits for child status changes. Supported flags are `WNOHANG`, `WUNTRACED`, and
`WCONTINUED`; `pid == -1` waits for any child.

### `sbrk(uint64_t old_brk, uint64_t new_brk)` - SVC 11

Validates that a requested heap range stays within `USER_HEAP_START` and
`USER_HEAP_SIZE`. The userspace allocator maintains the actual local heap break.

### `kill(pid_t pid, int signal)` - SVC 12

Sends a signal. Positive `pid` targets one process; negative process ids are
used by shell job control to signal a process group.

### `block_until_event(uint32_t events)` - SVC 13

Blocks the current thread until a kernel event bit is satisfied. Public event
bits include `BLOCK_UNTIL_NEW_CHILD` and `BLOCK_UNTIL_TTY_REQUEST`.

### Filesystem command syscalls - SVC 14 through 22

`touch`, `mv`, `rm`, `cat`, `cp`, `fs_chmod`, `ls`, `fs_mkdir`, and `cd` call
the high-level kernel filesystem command layer. New code should prefer the
descriptor APIs (`open`, `read`, `write`, `close`) when it needs stream I/O.

### Descriptor syscalls - SVC 23 through 27

`open`, `close`, `lseek`, `read`, and `write` operate on integer file
descriptors. Standard descriptors are `STDIN_FILENO == 0`, `STDOUT_FILENO == 1`,
and `STDERR_FILENO == 2`.

### Signal syscalls - SVC 28 through 33

The signal API uses `sigset_t` bitsets and `struct sigaction`. Handlers may be
`SIG_DFL`, `SIG_IGN`, or a function pointer of type `void (*)(int)`.

### Process-control syscalls - SVC 34 through 38

`fork`, `dup2`, `setpgid`, `getpgrp`, and `tcsetpgrp` support shell pipelines,
redirection, process groups, and foreground job control.

### Mount syscalls - SVC 39 and 40

The syscall slots exist in the kernel enum as `S_MOUNT` and `S_UNMOUNT`; the
userspace header names them `S_FS_MOUNT` and `S_FS_UNMOUNT`. The current
userspace `mount()` wrapper returns `0` without trapping, while `unmount()`
returns `-ENOSYS`.

### `pipe(int pipefd[2])` - SVC 41

Creates a unidirectional pipe. On success, `pipefd[0]` is the read end and
`pipefd[1]` is the write end.

### `ps(void)` - SVC 42

Prints or formats process information from the kernel process table. Returns
`0` or a negative errno.

### `exec(const char *path, char *const argv[])` - SVC 43

Loads an ELF image from `path` and replaces the current process image. On
success it does not return to the old program.

### `getcwd(char *path, size_t size)` - SVC 44

Copies the current working directory into `path`. The wrapper returns `path` on
success or an encoded negative errno cast to `char *`.

### `sleep(uint64_t ms)` - SVC 45

Sleeps the current thread for approximately `ms` milliseconds and lets other
threads run.

### `stat(const char *path, struct fs_stat_st *stat)` - SVC 46

Fills inode id, link count, type, permissions, size, block count, mtime, and
device numbers for a filesystem path. Works for disk files, procfs files, and
devfs character-device nodes.

### TTY syscalls - SVC 47 through 53

The TTY API supports multiple terminals, raw/canonical mode, screen sizing, and
alternate-screen presentation for fullscreen tools such as `vim`.

### `proc_change_priority(pid_t pid, int new_priority)` - SVC 54

Changes scheduler priority for a process. The wrapper returns the syscall
result; the dispatcher currently returns `0` after invoking the kernel helper.
