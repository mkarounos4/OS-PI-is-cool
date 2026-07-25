# Demo Guide

## Demo Goals

This demo shows the OS booting, entering userspace, running shell commands, using the filesystem, launching ELF programs, exercising process control, inspecting `/proc`, and displaying UART/framebuffer terminal output.

## Recommended Demo Flow

1. Boot on QEMU or Raspberry Pi 5.
2. Show the kernel boot log and transition to userspace.
3. Start the shell.
4. Run filesystem commands.
5. Run ELF userspace programs from `/bin`.
6. Demonstrate `fork`, `exec`, `waitpid`, and pipes through shell command execution.
7. Demonstrate signals and job control.
8. Inspect `/proc` diagnostics.
9. Show framebuffer terminal and multi-terminal behavior, if available.
10. Demonstrate persistence across reboot, if using a persistent SD card or QEMU disk image.

## Example Commands

Use commands from the current `user/cmds` tree:

```sh
ls /
ls /bin
ls /proc
cat /proc/processes
cat /proc/meminfo
cat /proc/vmstat
cat /proc/threads
cat /proc/interrupts
cat /proc/syscalls
cat /proc/cache
cat /proc/tty
```

Filesystem read/write flow:

```sh
echo hello > hello.txt
cat hello.txt
grep hello hello.txt
wc hello.txt
stat hello.txt
mkdir notes
cp hello.txt notes/copy.txt
ln hello.txt hello-hard.txt
ln -s notes notes-link
readlink notes-link
ls notes-link
ls notes
rm notes/copy.txt
```

Process, pipes, and job-control flow:

```sh
ps
cat hello.txt | grep hello
sleep 5000 &
jobs
ps
kill <pid>
```

Interactive utility flow:

```sh
vim notes.txt
cat notes.txt
free
getcwd
clear
```

Replace `<pid>` with a live process id shown by `ps`, `jobs`, or `/proc/processes`.

## Screenshots / Video Checklist

- Boot log
- Shell startup
- `/bin` command listing
- Filesystem read/write
- Process and job-control demo
- `/proc` output
- Framebuffer terminal
- Multiple terminal tabs
- Editor utility
- QEMU run and Raspberry Pi 5 hardware run, if both are available

## Notes for Recording

- Keep terminal text large enough to read in a portfolio clip.
- Start from a clean boot so the kernel initialization order is visible.
- Prefer short clips per subsystem over one long recording.
- Show both QEMU and Raspberry Pi 5 hardware when possible.
- When showing persistence, create a small file, reboot, then read the same file again.
- Avoid spending time on setup steps in the final video; keep the focus on OS behavior.
