#include "syscall/syscall.h"

#include <stddef.h>

#include "timer/timer.h"
#include "traps/traps.h"
#include "uart/uart.h"
#include "scheduler/scheduler.h"
#include "signals/signals.h"
#include "memory/page_table/page_table.h"
#include "fs/cmds.h"
#include "fs/kapi.h"
#include "fs/errors.h"
#include "pipe/pipe.h"

#define SYS_WRITE_CONSOLE_MAX 1024u
#define SYS_USER_PTR_MIN      UINT64_C(0x1000)
#define SYS_WRITE_CHUNK       128u
#define SYSCALL_COUNT         47u

static uint32_t syscall_counts[SYSCALL_COUNT];

static const char *syscall_name(uint64_t syscall_number) {
    static const char *names[SYSCALL_COUNT] = {
        [S_WRITE_CONSOLE] = "write_console",
        [S_PUTC] = "putc",
        [S_GET_TICKS] = "get_ticks",
        [S_YIELD] = "yield",
        [S_EXIT] = "exit",
        [S_GETPID] = "getpid",
        [S_CURRENT_EL] = "current_el",
        [S_DELAY] = "delay",
        [S_SPAWN] = "spawn",
        [S_WAITPID] = "waitpid",
        [S_SBRK] = "sbrk",
        [S_KILL] = "kill",
        [S_BLOCK_UNTIL_EVENT] = "block_until_event",
        [S_FS_TOUCH] = "touch",
        [S_FS_MV] = "mv",
        [S_FS_RM] = "rm",
        [S_FS_CAT] = "cat",
        [S_FS_CP] = "cp",
        [S_FS_CHMOD] = "chmod",
        [S_FS_LS] = "ls",
        [S_FS_MKDIR] = "mkdir",
        [S_FS_CD] = "cd",
        [S_FS_OPEN] = "open",
        [S_FS_CLOSE] = "close",
        [S_FS_LSEEK] = "lseek",
        [S_FS_READ] = "read",
        [S_FS_WRITE] = "write",
        [S_SIGPROCMASK] = "sigprocmask",
        [S_SIGEMPTYSET] = "sigemptyset",
        [S_SIGADDSET] = "sigaddset",
        [S_SIGFILLSET] = "sigfillset",
        [S_SIGSUSPEND] = "sigsuspend",
        [S_SIGACTION] = "sigaction",
        [S_FORK] = "fork",
        [S_DUP2] = "dup2",
        [S_SETPGID] = "setpgid",
        [S_GETPGRP] = "getpgrp",
        [S_TCSETPGRP] = "tcsetpgrp",
        [S_MOUNT] = "mount",
        [S_UNMOUNT] = "unmount",
        [S_PIPE] = "pipe",
        [S_PS] = "ps",
        [S_EXEC] = "exec",
        [S_GETCWD] = "getcwd",
        [S_SLEEP] = "sleep",
        [S_STAT] = "stat",
    };

    if (syscall_number >= SYSCALL_COUNT ||
        names[syscall_number] == NULL) {
        return "unknown";
    }
    return names[syscall_number];
}

static long s_getpid() {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc == NULL) {
        return SYS_ESRCH;
    }
    return (long) curr_proc->pid;
}

static long sys_write_console_impl(const char *s, uint64_t len) {
    uint64_t written = 0;

    if (s == NULL && len != 0) {
        return SYS_EFAULT;
    }

    if ((uintptr_t)s < SYS_USER_PTR_MIN && len != 0) {
        return SYS_EFAULT;
    }

    if (len > SYS_WRITE_CONSOLE_MAX) {
        len = SYS_WRITE_CONSOLE_MAX;
    }

    while (written < len) {
        uart_putc(s[written]);
        written++;
    }

    return (long)written;
}

static void __attribute__((noreturn)) s_exit_impl(int code) {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc != NULL) {
        curr_proc->state = PROC_ZOMBIE_STATE;
        curr_proc->exit_code = code;
        send_sigchld(curr_proc->pid);
    }    

    schedule_yield();
    while (1) {
        asm volatile("wfe");
    }
}

static long s_spawn(void* (*func)(void*), void *argv) {
    pid_t ppid = s_getpid();
    
    pid_t new_proc = proc_create(func, argv, ppid);
    if (new_proc < 0) {
        return new_proc;
    }
    pcb_t *new_pcb = get_pcb_by_pid(new_proc);
    if (new_pcb == NULL) {
        return SYS_ESRCH;
    }
    add_task_to_scheduler(new_pcb);

    return new_pcb->pid;
}

static struct trap_frame *s_block_until_event(uint32_t event,
                                              struct trap_frame *frame) {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc == NULL) {
        frame->regs[0] = (uint64_t)SYS_ESRCH;
        return frame;
    }

    curr_proc->blocked_until = event;
    frame->regs[0] = 0;
    block_process(curr_proc);

    return frame;
}

static long s_sbrk_validate(uint64_t old_brk, uint64_t new_brk) {
    if (get_curr_process() == NULL) {
        return SYS_EINVAL;
    }

    if (old_brk > new_brk) {
        return SYS_EINVAL;
    }

    if (old_brk < USER_HEAP_START) {
        return SYS_EINVAL;
    }

    if (new_brk > USER_HEAP_START + USER_HEAP_SIZE) {
        return SYS_EINVAL;
    }

    return 0;
}

struct trap_frame *syscall_dispatch(struct trap_frame *frame) {
    uint64_t syscall_number = frame->regs[8];
    if (syscall_number < SYSCALL_COUNT) {
        syscall_counts[syscall_number]++;
    }
    long ret = SYS_ENOSYS;

    switch (syscall_number) {
    case S_WRITE_CONSOLE:
        ret = sys_write_console_impl((const char *)(uintptr_t)frame->regs[0], frame->regs[1]);
        break;

    case S_PUTC:
        uart_putc((char)frame->regs[0]);
        ret = 0;
        break;

    case S_GET_TICKS:
        ret = (long)timer_get_ticks();
        break;

    case S_YIELD:
        frame->regs[0] = 0;
        schedule_yield();
        return frame;

    case S_EXIT:
        s_exit_impl((int)frame->regs[0]);
    case S_GETPID:
        ret = s_getpid();
        break;
    case S_CURRENT_EL:
    case S_DELAY:
        timer_delay_ms((int)frame->regs[0]);
        ret = 0;
        break;
    case S_SPAWN:
        ret = s_spawn((void*(*)(void*))frame->regs[0], (void*) frame->regs[1]);
        break;
    case S_WAITPID:
        ret = s_waitpid_impl((pid_t)frame->regs[0],
                             (int *)(uintptr_t)frame->regs[1],
                             (int32_t)frame->regs[2]);
        break;
    case S_SBRK:
        ret = s_sbrk_validate(frame->regs[0], frame->regs[1]);
        break;
    case S_KILL:
        ret = s_kill((pid_t)frame->regs[0], (int)(uintptr_t)frame->regs[1]);
        break;
    case S_BLOCK_UNTIL_EVENT:
        return s_block_until_event((uint32_t) frame->regs[0], frame);
    case S_FS_TOUCH:
        ret = fs_err_to_sys_errno(touch((char **)(uintptr_t)frame->regs[0]));
        break;
    case S_FS_MV:
        ret = fs_err_to_sys_errno(mv((char *)(uintptr_t)frame->regs[0],
                                   (char *)(uintptr_t)frame->regs[1]));
        break;
    case S_FS_RM:
        ret = fs_err_to_sys_errno(rm((char **)(uintptr_t)frame->regs[0]));
        break;
    case S_FS_CAT:
        ret = fs_err_to_sys_errno(cat((char **)(uintptr_t)frame->regs[0],
                                    (char *)(uintptr_t)frame->regs[1],
                                    (int)frame->regs[2]));
        break;
    case S_FS_CP:
        ret = fs_err_to_sys_errno(cp((char *)(uintptr_t)frame->regs[0],
                                   (char *)(uintptr_t)frame->regs[1],
                                   (int)frame->regs[2]));
        break;
    case S_FS_CHMOD:
        ret = fs_err_to_sys_errno(fs_chmod((char *)(uintptr_t)frame->regs[0],
                                         (char *)(uintptr_t)frame->regs[1],
                                         (int)frame->regs[2]));
        break;
    case S_FS_LS:
        ret = fs_err_to_sys_errno(ls((char *)(uintptr_t)frame->regs[0]));
        break;
    case S_FS_MKDIR:
        ret = fs_err_to_sys_errno(fs_mkdir((char **)(uintptr_t)frame->regs[0]));
        break;
    case S_FS_CD:
        ret = fs_err_to_sys_errno(cd((char *)(uintptr_t)frame->regs[0]));
        break;
    case S_FS_OPEN:
        ret = fs_err_to_sys_errno(open((const char *)(uintptr_t)frame->regs[0],
                                     (int)frame->regs[1]));
        break;
    case S_FS_CLOSE:
        ret = fs_err_to_sys_errno(close((int)frame->regs[0]));
        break;
    case S_FS_LSEEK:
        ret = fs_err_to_sys_errno(lseek((int)frame->regs[0],
                                      (int)frame->regs[1],
                                      (int)frame->regs[2]));
        break;
    case S_FS_READ:
        ret = fs_err_to_sys_errno(read((int)frame->regs[0],
                                     (char *)(uintptr_t)frame->regs[1],
                                     (int)frame->regs[2]));
        break;
    case S_FS_WRITE:
        ret = fs_err_to_sys_errno(write((int)frame->regs[0],
                                      (char *)(uintptr_t)frame->regs[1],
                                      (int)frame->regs[2]));
        break;
    case S_SIGPROCMASK:
        ret = sigprocmask((int)frame->regs[0],
                          (const signalset_t *)(uintptr_t)frame->regs[1],
                          (signalset_t *)(uintptr_t)frame->regs[2]);
        break;
    case S_SIGEMPTYSET:
        ret = sigemptyset((signalset_t *)(uintptr_t)frame->regs[0]);
        break;
    case S_SIGADDSET:
        ret = sigaddset((signalset_t *)(uintptr_t)frame->regs[0], (int)frame->regs[1]);
        break;
    case S_SIGFILLSET:
        ret = sigfillset((signalset_t *)(uintptr_t)frame->regs[0]);
        break;
    case S_SIGSUSPEND:
        ret = sigsuspend((const signalset_t *)(uintptr_t)frame->regs[0]);
        break;
    case S_SIGACTION:
        ret = sigaction((int)frame->regs[0],
                        (struct sigaction *)(uintptr_t)frame->regs[1],
                        (struct sigaction *)(uintptr_t)frame->regs[2]);
        break;
    case S_FORK:
        ret = fork(frame);
        break;
    case S_DUP2:
        ret = dup2((pid_t)frame->regs[0], (pid_t)frame->regs[1]);
        break;
    case S_SETPGID:
        ret = setpgrp((pid_t)frame->regs[0], (pid_t)frame->regs[1]);
        break;
    case S_GETPGRP:
        ret = getpgid();
        break;
    case S_TCSETPGRP:
        ret = tcsetpgrp((int)frame->regs[0], (pid_t)frame->regs[1]);
        break;
    case S_PIPE:
        ret = pipe((int*)frame->regs[0]);
        break;
    case S_PS:
        ret = ps();
        break;
    case S_EXEC:
    {
        struct trap_frame *next_frame = frame;
        ret = k_exec((char *)(uintptr_t)frame->regs[0],
                     (char **)(uintptr_t)frame->regs[1],
                     frame,
                     &next_frame);
        if (ret == 0) {
            return next_frame;
        }
        ret = fs_err_to_sys_errno(ret);
        break;
    }
    case S_GETCWD:
        ret = fs_err_to_sys_errno(getcwd((char *)(uintptr_t)frame->regs[0],
                                         (size_t)frame->regs[1]));
        break;
    case S_SLEEP:
        ret = SYS_ENOSYS;
        break;
    case S_STAT:
        ret = fs_err_to_sys_errno(k_stat((const char *)(uintptr_t)frame->regs[0],
                                         (struct fs_stat_st *)(uintptr_t)frame->regs[1]));
        break;
    default:
        ret = SYS_ENOSYS;
        break;
    }

    frame->regs[0] = (uint64_t)ret;
    return frame;
}

int syscall_format_proc(char *buf, size_t size) {
    if (buf == NULL || size == 0) {
        return SYS_EINVAL;
    }

    int len = snprintf(buf, size, "NR COUNT NAME\n");
    for (uint32_t nr = 0; nr < SYSCALL_COUNT; nr++) {
        if (syscall_counts[nr] == 0) {
            continue;
        }

        size_t used = len < (int)size ? (size_t)len : size - 1;
        int ret = snprintf(buf + used, size - used, "%u %u %s\n",
                           nr, syscall_counts[nr], syscall_name(nr));
        if (ret < 0) {
            return ret;
        }
        len += ret;
    }

    return len;
}
