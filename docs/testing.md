# Testing and Validation

This project currently uses a mix of userspace smoke tests, QEMU/hardware boot
checks, shell-driven manual tests, and `/proc` diagnostics. The validation
strategy is intentionally practical: each subsystem should be demonstrable
through the same user/kernel paths that normal programs use.

## Smoke Tests

Existing userspace smoke-test helpers live in
[`user/lib/tests.c`](../user/lib/tests.c):

- `malloc_lazy_test`: allocates more than one page, writes across the allocation,
  and checks that lazy heap allocation works.
- `waitpid_signal_test`: spawns child processes, sends `SIGSTOP`, `SIGCONT`, and
  `SIGKILL`, and exercises `waitpid(WUNTRACED)` status handling.
- `scheduler_orphan_test`: spawns test processes and an orphan-style workload for
  scheduler/process-lifecycle inspection. It is present but commented out in
  `init.c` by default.

The current `init` program calls `malloc_lazy_test()` and
`waitpid_signal_test()` during boot before entering its shell-supervisor loop.
Those tests are useful smoke coverage, not a replacement for a full automated
regression suite.

## Manual Demo Tests

These flows validate the public OS behavior through userspace:

- QEMU boot with `make qemu`
- Raspberry Pi 5 boot from the SD-card flow in [quickstart.md](quickstart.md)
- Shell startup on `/dev/tty0`
- `/bin` command listing with `ls /bin`
- Filesystem creation, read/write, copy, remove, and metadata checks with
  `echo`, `cat`, `cp`, `rm`, `stat`, `ln`, `readlink`, and `ls`
- Persistent file check across reboot on a persistent SD card or QEMU disk image
- Pipes and redirection through shell commands such as `cat file | grep pattern`
  and `echo text > file`
- Process groups and job control with `sleep 5000 &`, `jobs`, `ps`, and `kill`
- Framebuffer terminal output, UART-backed input, and terminal tab creation where
  available
- Fullscreen TTY path with `vim`

## Debugging Interfaces

The `/proc` filesystem exposes live kernel state through ordinary file reads.
Useful files include:

- `/proc/processes`
- `/proc/threads`
- `/proc/meminfo`
- `/proc/vmstat`
- `/proc/interrupts`
- `/proc/syscalls`
- `/proc/cache`
- `/proc/tty`
- `/proc/timers`
- `/proc/locks`
- `/proc/mounts`
- `/proc/version`
- `/proc/cpuinfo`
- `/proc/<pid>/status`
- `/proc/<pid>/fd`
- `/proc/<pid>/maps`
- `/proc/<pid>/threads`

These files are especially useful when validating cross-subsystem behavior. For
example, a shell pipeline can be inspected through `/proc/processes`,
`/proc/<pid>/fd`, `/proc/syscalls`, and `/proc/vmstat` to confirm that process
creation, descriptor setup, syscall dispatch, and demand paging all participated
in the same workflow.

## Validation Philosophy

The most valuable tests for this codebase are end-to-end because many features
are only meaningful when subsystems interact. `fork` depends on copy-on-write
page tables, file-descriptor inheritance, scheduler state, and trap-frame return
semantics. `exec` depends on VFS path lookup, permissions, ELF validation,
argument stack setup, lazy segment paging, and signal-disposition reset rules.
Job control depends on process groups, TTY foreground ownership, signals, and
`waitpid`.

Manual validation should therefore include both narrow checks and complete
workflows. A good smoke pass boots the OS, reaches the shell, runs commands from
`/bin`, reads and writes files, exercises a pipeline, starts and kills a
background job, opens `/proc` diagnostics, and confirms output on the selected
TTY backend.
