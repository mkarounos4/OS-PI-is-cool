# Userspace API Reference

## Userspace Library Table

| Header | Function or macro | Backing syscall/SVC | Summary |
|---|---|---:|---|
| `lib/syscall.h` | `sys_call0` ... `sys_call6` | raw | Inline AArch64 `svc #0` helpers. |
| `lib/syscall.h` | `write_console` | 1 | Console-only byte write. |
| `lib/syscall.h` | `putc` | 2 | Console-only character write. |
| `lib/syscall.h` | `get_ticks` | 3 | Read timer ticks. |
| `lib/syscall.h` | `delay` | 8 | Delay for milliseconds. |
| `lib/syscall.h` | `sleep` | 45 | Sleep for milliseconds. |
| `lib/syscall.h` | `exit` | 5 | Terminate process. |
| `lib/syscall.h` | `getpid` | 6 | Return pid. |
| `lib/syscall.h` | `spawn` | 9 | Spawn function-entry process. |
| `lib/syscall.h` | `waitpid` | 10 | Wait for child status. |
| `lib/syscall.h` | `sbrk` | 11 | Validate heap break range. |
| `lib/syscall.h` | `kill` | 12 | Send signal. |
| `lib/syscall.h` | `block_until_event` | 13 | Block on kernel event bits. |
| `lib/syscall.h` | `tty_next_request` | 47 | Get requested terminal id. |
| `lib/syscall.h` | `putstr` | 1 | Write a NUL-terminated string to console. |
| `lib/syscall.h` | `puthex` | 1, 2 | Write a 64-bit value in hexadecimal to console. |
| `lib/syscall.h` | `fork` | 34 | Copy current process. |
| `lib/syscall.h` | `dup2` | 35 | Duplicate descriptor. |
| `lib/syscall.h` | `setpgid` | 36 | Set process group. |
| `lib/syscall.h` | `getpgrp` | 37 | Return current process group. |
| `lib/syscall.h` | `tcsetpgrp` | 38 | Set terminal foreground group. |
| `lib/syscall.h` | `mount` | none | Stub returning `0`. |
| `lib/syscall.h` | `unmount` | none | Stub returning `-ENOSYS`. |
| `lib/syscall.h` | `pipe` | 41 | Create pipe descriptors. |
| `lib/syscall.h` | `ps` | 42 | Process listing syscall. |
| `lib/syscall.h` | `exec` | 43 | Execute ELF path. |
| `lib/syscall.h` | `proc_change_priority` | 54 | Change scheduler priority. |
| `lib/fs_syscall.h` | `open` | 23 | Open path with flags. |
| `lib/fs_syscall.h` | `close` | 24 | Close descriptor. |
| `lib/fs_syscall.h` | `lseek` | 25 | Seek descriptor. |
| `lib/fs_syscall.h` | `read` | 26 | Read descriptor. |
| `lib/fs_syscall.h` | `write` | 27 | Write descriptor. |
| `lib/fs_syscall.h` | `touch` | 14 | Create/touch paths. |
| `lib/fs_syscall.h` | `mv` | 15 | Rename path. |
| `lib/fs_syscall.h` | `rm` | 16 | Remove paths. |
| `lib/fs_syscall.h` | `cat` | 17 | Kernel cat helper. |
| `lib/fs_syscall.h` | `cp` | 18 | Kernel copy helper. |
| `lib/fs_syscall.h` | `fs_chmod` | 19 | Change permissions. |
| `lib/fs_syscall.h` | `ls` | 20 | List directory. |
| `lib/fs_syscall.h` | `fs_mkdir` | 21 | Create directories. |
| `lib/fs_syscall.h` | `cd` | 22 | Change cwd. |
| `lib/fs_syscall.h` | `getcwd` | 44 | Return cwd string. |
| `lib/fs_syscall.h` | `stat` | 46 | Read path metadata. |
| `lib/tty_syscall.h` | `tty_get_mode` | 48 | Read raw/canonical mode. |
| `lib/tty_syscall.h` | `tty_set_mode` | 49 | Set raw/canonical mode. |
| `lib/tty_syscall.h` | `tty_get_size` | 50 | Read rows and columns. |
| `lib/tty_syscall.h` | `tty_screen_enter` | 51 | Enter alternate screen. |
| `lib/tty_syscall.h` | `tty_screen_leave` | 52 | Leave alternate screen. |
| `lib/tty_syscall.h` | `tty_screen_present` | 53 | Present screen cells. |
| `lib/signals.h` | `sigprocmask` | 28 | Change signal mask. |
| `lib/signals.h` | `sigemptyset` | 29 | Empty signal set. |
| `lib/signals.h` | `sigaddset` | 30 | Add signal to set. |
| `lib/signals.h` | `sigfillset` | 31 | Fill signal set. |
| `lib/signals.h` | `sigsuspend` | 32 | Suspend with mask. |
| `lib/signals.h` | `sigaction` | 33 | Install/read handler. |
| `lib/stdio.h` | `printf` | 27 | Formatted output to stdout. |
| `lib/stdio.h` | `puts` | 27 | Print string and newline. |
| `lib/string.h` | `strlen` | none | String length. |
| `lib/string.h` | `strcmp` | none | Lexicographic string compare. |
| `lib/string.h` | `strtol` | none | Parse integer text. |
| `lib/string.h` | `str_concat` | none | Allocate and concatenate two strings. |
| `lib/string.h` | `str_copy` | none | Allocate and copy a string. |
| `lib/string.h` | `isspace` | none | Test ASCII whitespace. |
| `lib/malloc.h` | `malloc` | allocator | Allocate heap memory. |
| `lib/malloc.h` | `realloc` | allocator | Resize allocation. |
| `lib/malloc.h` | `calloc` | allocator | Allocate zeroed array. |
| `lib/malloc.h` | `free` | allocator | Release allocation. |
| `lib/malloc.h` | `memcpy` | none | Copy bytes. |
| `lib/malloc.c` | `memset` | none | Fill bytes; implemented but not declared in `malloc.h`. |
| `lib/malloc.h` | `mm_init` | allocator | Initialize allocator metadata. |
| `lib/malloc.h` | `mem_init` | allocator | Bind allocator to a heap range. |
| `lib/malloc.h` | `mem_sbrk` | allocator | Advance allocator heap break. |
| `lib/malloc.h` | `mem_heap_lo` | allocator | Return heap base. |
| `lib/malloc.h` | `mem_heap_hi` | allocator | Return last allocated heap byte. |
| `lib/errno.h` | `errno_name` | none | Convert errno to symbolic name. |
| `lib/errno.h` | `errno_message` | none | Convert errno to human message. |
| `lib/errno.h` | `print_errno` | 27 | Print command-style errno message. |
| `lib/tests.h` | `scheduler_orphan_test` | mixed | Spawn scheduler/orphan smoke-test processes. |
| `lib/tests.h` | `waitpid_signal_test` | mixed | Exercise waitpid with stop/continue/kill status. |
| `lib/tests.h` | `malloc_lazy_test` | allocator | Exercise lazy heap allocation. |
| `cmds/shell.h` | `shell_init` | mixed | Initialize shell state for a TTY. |
| `cmds/shell.h` | `perror` | 27 | Minimal inline stderr writer. |
| `cmds/shell/parser.h` | `parse_command` | allocator | Parse command text into `struct parsed_command`. |
| `cmds/shell/parser.h` | `print_parsed_command` | 27 | Debug-print a parsed command. |
| `cmds/shell/parser.h` | `print_parser_errcode` | 27 | Debug-print parser errors. |
| `cmds/shell/Vec.h` | `vec_new` | allocator | Create a dynamic array. |
| `cmds/shell/Vec.h` | `vec_get` | none | Return an element by index. |
| `cmds/shell/Vec.h` | `vec_set` | none | Replace an element by index. |
| `cmds/shell/Vec.h` | `vec_push_back` | allocator | Append an element, resizing as needed. |
| `cmds/shell/Vec.h` | `vec_pop_back` | none | Remove and optionally return the last element. |
| `cmds/shell/Vec.h` | `vec_insert` | allocator | Insert an element at an index. |
| `cmds/shell/Vec.h` | `vec_erase` | none | Remove an element by index. |
| `cmds/shell/Vec.h` | `vec_resize` | allocator | Resize vector storage. |
| `cmds/shell/Vec.h` | `vec_clear` | none | Remove all elements and run destructors. |
| `cmds/shell/Vec.h` | `vec_destroy` | allocator | Clear and free vector storage. |
| `cmds/shell/Job.h` | `get_job_by_pid` | none | Find a shell job containing a pid. |
| `cmds/shell/Job.h` | `vec_remove_job_by_id` | none | Remove a shell job from a vector by job id. |
| `cmds/shell/Job.h` | `free_job` | allocator | Free a shell job object. |
| `cmds/shell/Job.h` | `get_job_bg_fg` | mixed | Resolve the default or requested job for `bg`/`fg`. |
| `cmds/shell/Job.h` | `free_dtor` | allocator | Generic freeing destructor for vectors. |
| `cmds/shell/io-helpers.h` | `changeStdInput` | 23, 24, 35 | Apply parsed stdin redirection. |
| `cmds/shell/io-helpers.h` | `changeStdOutput` | 23, 24, 35 | Apply parsed stdout redirection. |

## Userspace Function Notes

### Raw syscall helpers

`sys_call0` through `sys_call6` load the syscall number into `x8`, load up to
six integer/pointer arguments into `x0` through `x5`, execute `svc #0`, and
return `x0`.

### Console helpers

`write_console`, `putc`, `putstr`, and `puthex` bypass file descriptors and
write directly to the console path. Prefer `write(STDOUT_FILENO, ...)` for
normal programs.

### Process helpers

`fork`, `exec`, `exit`, `spawn`, `waitpid`, `getpid`, `setpgid`, `getpgrp`,
`tcsetpgrp`, `kill`, `sleep`, `delay`, `block_until_event`, and
`proc_change_priority` wrap process, scheduler, and job-control syscalls.

### Filesystem helpers

`open`, `close`, `lseek`, `read`, `write`, `getcwd`, and `stat` are the normal
file-descriptor API. `touch`, `mv`, `rm`, `cat`, `cp`, `fs_chmod`, `ls`,
`fs_mkdir`, and `cd` expose higher-level kernel filesystem commands.

Open flags are `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_TRUNC`, `O_CREAT`, and
`O_APPEND`. Seek modes are `F_SEEK_SET`, `F_SEEK_CUR`, and `F_SEEK_END`.
TTY devices are exposed as `/dev/ttyN`; backend devices are exposed as
`/dev/uart0` and `/dev/ttyguiN` for direct testing. These paths are devfs
virtual nodes, not persistent disk files. Normal programs should use `/dev/ttyN`
unless they intentionally want backend-level I/O.

### TTY helpers

`tty_get_mode`, `tty_set_mode`, `tty_get_size`, `tty_screen_enter`,
`tty_screen_leave`, and `tty_screen_present` are used by fullscreen programs.
Modes are `TTY_MODE_CANONICAL` and `TTY_MODE_RAW`.

### Signal helpers

`sigprocmask`, `sigemptyset`, `sigaddset`, `sigfillset`, `sigsuspend`, and
`sigaction` provide a small POSIX-like signal surface. Defined signals include
`SIGINT`, `SIGTTOU`, `SIGTTIN`, `SIGTSTP`, `SIGKILL`, `SIGSTOP`, `SIGCONT`,
`SIGCHLD`, and `SIGTERM`.

### Formatted output

`printf` supports `%s`, `%d`, `%u`, `%x`, `%X`, and `%%`. `puts` prints one
string followed by a newline. Both write to `STDOUT_FILENO`.

### Strings

`strlen`, `strcmp`, `strtol`, and `isspace` are minimal libc-style helpers.
`str_concat` and `str_copy` allocate their result with `malloc`; callers own the
returned buffer.

### Heap allocator

`mem_init` initializes the userspace heap range, `mm_init` initializes allocator
metadata, and `malloc`/`free`/`realloc`/`calloc` manage 16-byte-aligned heap
blocks. `mem_heap_lo`, `mem_heap_hi`, and `mem_sbrk` expose allocator internals
used by tests and diagnostics.

### Error helpers

`errno_name` and `errno_message` accept either positive or negative errno values.
`print_errno(cmd, context, err)` writes `cmd: context: ENAME (-N): message` to
stderr when `err` is negative.

### Test helpers

`scheduler_orphan_test`, `waitpid_signal_test`, and `malloc_lazy_test` are
userspace smoke-test entry points used by `init.c`. They print status through
console helpers and exercise process, signal, waitpid, and allocator behavior.

### Shell support helpers

`parse_command` builds a heap-allocated `struct parsed_command` for pipelines,
redirection, background jobs, and here-documents. The shell `Vec` functions
provide a small dynamic array, and the `Job` helpers find, remove, resume, and
free job-control records. `changeStdInput` and `changeStdOutput` apply parsed
redirections with `open`, `dup2`, and `close`.

## User Commands

| Command | Usage | Summary |
|---|---|---|
| `cat` | `cat [file...] [-a file|-w file]` | Copy files or stdin to stdout or an output file. |
| `chmod` | `chmod [-|+|=]mode file` | Change permissions using set, add, or remove-style modes. |
| `clear` | `clear` | Write a form-feed to clear the terminal. |
| `cp` | `cp src dest` | Copy one file to another. |
| `echo` | `echo [arg...]` | Print arguments separated by spaces. |
| `free` | `free` | Print userspace heap total, used, and free bytes. |
| `getcwd` | `getcwd` | Print the current working directory. |
| `grep` | `grep pattern [file...]` | Print matching lines from files or stdin. |
| `init` | `init` | First userspace process; starts shells and reaps children. |
| `kill` | `kill [-signal] pid` | Send a signal to a process. |
| `ls` | `ls [dir]` | List a directory, defaulting to the current directory. |
| `mkdir` | `mkdir dir...` | Create one or more directories. |
| `mv` | `mv src dest` | Move or rename a path. |
| `ps` | `ps` | Display process information. |
| `rm` | `rm path...` | Remove paths. |
| `shell` | `shell [tty_number]` | Interactive shell with pipelines, redirection, scripts, and job control. |
| `sleep` | `sleep milliseconds` | Sleep for a number of milliseconds. |
| `stat` | `stat file...` | Print inode metadata for paths. |
| `touch` | `touch file...` | Create files or update metadata. |
| `vim` | `vim file` | Small fullscreen text editor. |
| `wc` | `wc [file...]` | Count lines, words, and bytes. |

### `cat`

Reads each named file and writes it to stdout. With no input files, reads stdin.
`-w file` truncates/creates the output file; `-a file` appends/creates it.

### `chmod`

Calls `fs_chmod`. A leading `+` adds permissions, `-` removes permissions, and
`=` or no prefix sets permissions.

### `clear`

Writes `\f` to stdout; terminal drivers interpret this as a screen clear.

### `cp`

Copies bytes from `src` to `dest` using `open`, `read`, `write`, and `close`.
The destination is created or truncated.

### `echo`

Writes its arguments separated by single spaces and ends with a newline.

### `free`

Reports the fixed userspace heap size (`USER_HEAP_SIZE`) and allocator high-water
usage from `mem_heap_lo`/`mem_heap_hi`.

### `getcwd`

Calls `getcwd` with a 256-byte buffer and prints the returned path.

### `grep`

Performs a simple substring match. With only a pattern, reads stdin; otherwise
processes each named file independently.

### `init`

Ignores signals, starts `/bin/shell` on tty 0, handles TTY shell requests, and
reaps children with `waitpid(-1, ..., WNOHANG)`.

### `kill`

Defaults to `SIGTERM`. A numeric `-signal` argument overrides the signal number.

### `ls`

Lists the requested directory or the current directory when no path is supplied.

### `mkdir`

Creates each named directory path through the filesystem command syscall.

### `mv`

Moves or renames `src` to `dest`.

### `ps`

Invokes the process-listing syscall.

### `rm`

Removes every path passed after the command name.

### `shell`

Provides an interactive shell with foreground/background jobs, process groups,
pipelines, redirection, here-documents, and `#!/bin/sh` script execution.
Builtins include `jobs`, `bg`, `fg`, `cd`, `pwd`, and `clear`.

### `sleep`

Parses a non-negative millisecond value and calls the `sleep` syscall.

### `stat`

Prints inode id, type, permissions, link count, size, block count, modification
time, and device numbers for character devices.

### `touch`

Creates or updates one or more filesystem paths.

### `vim`

Uses raw TTY mode and alternate-screen presentation. Normal mode supports
`h/j/k/l`, `x`, `i`, and `:`. Command mode supports `:w`, `:q`, `:q!`, and
`:wq`.

### `wc`

Counts lines, words, and bytes from stdin or each named file. When multiple
files are provided, prints a total line.

## Shell Support API

| Header | Function/type | Summary |
|---|---|---|
| `cmds/shell.h` | `shell_init` | Initialize shell process state for a TTY. |
| `cmds/shell.h` | `perror` | Minimal inline stderr writer. |
| `cmds/shell/parser.h` | `parse_command` | Parse command text into `struct parsed_command`. |
| `cmds/shell/parser.h` | `print_parsed_command` | Debug-print a parsed command. |
| `cmds/shell/parser.h` | `print_parser_errcode` | Debug-print parser errors. |
| `cmds/shell/Vec.h` | `Vec` and `vec_*` | Dynamic array used by shell jobs. |
| `cmds/shell/Job.h` | `job` and job helpers | Shell job bookkeeping and lookup helpers. |
| `cmds/shell/io-helpers.h` | `changeStdInput` | Apply parsed stdin redirection. |
| `cmds/shell/io-helpers.h` | `changeStdOutput` | Apply parsed stdout redirection. |
