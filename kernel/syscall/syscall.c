#include "syscall/syscall.h"

#include <stddef.h>

#include "timer/timer.h"
#include "traps/traps.h"
#include "uart/uart.h"
#include "scheduler/scheduler.h"

#define SYS_WRITE_CONSOLE_MAX 1024u
#define SYS_USER_PTR_MIN      UINT64_C(0x1000)

static long s_getpid() {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc == NULL) {
        return -1;
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

static struct trap_frame *s_exit_impl(struct trap_frame *frame, int code) {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc != NULL) {
        curr_proc->state = PROC_ZOMBIE_STATE;
        curr_proc->exit_code = code;
        curr_proc->frame = frame;
        // call SIGCHILD here
    }    

    return schedule_next_task();
}

static long s_spawn(void* (*func)(void*), void *argv) {
    pid_t ppid = s_getpid();
    
    pid_t new_proc = proc_create(func, argv, ppid);
    if (new_proc < 0) {
        return new_proc;
    }
    pcb_t *new_pcb = get_pcb_by_pid(new_proc);
    if (new_pcb == NULL) {
        return -1;
    }

    add_task_to_scheduler(new_pcb);
    return new_pcb->pid;
}

static long s_waitpid() {
    return 0;
}

long s_kill(pid_t pid, int signal) {
    (void)pid;
    (void)signal;
    return 0;
}

struct trap_frame *syscall_dispatch(struct trap_frame *frame) {
    uint64_t syscall_number = frame->regs[8];
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
        break;

    case S_EXIT:
        return s_exit_impl(frame, (int)frame->regs[0]);
    case S_GETPID:
        ret = s_getpid();
        break;
    case S_CURRENT_EL:
    case S_DELAY:
        ret = SYS_ENOSYS;
        break;
    case S_SPAWN:
        ret = s_spawn((void*(*)(void*))frame->regs[0], (void*) frame->regs[1]);
        break;
    case S_WAITPID:
        ret = s_waitpid();
        break;
    case S_KILL:
        ret = s_kill((pid_t)frame->regs[0], (int)(uintptr_t)frame->regs[1]);
        break;

    default:
        ret = SYS_ENOSYS;
        break;
    }

    frame->regs[0] = (uint64_t)ret;
    return frame;
}
