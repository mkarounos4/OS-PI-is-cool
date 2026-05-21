#include "syscall/syscall.h"

#include <stddef.h>

#include "timer/timer.h"
#include "traps/traps.h"
#include "uart/uart.h"
#include "scheduler/scheduler.h"
#include "memory/kernel_mem.h"
#include "signals/signals.h"

#define SYS_WRITE_CONSOLE_MAX 1024u
#define SYS_USER_PTR_MIN      UINT64_C(0x1000)
#define SYS_WRITE_CHUNK       128u

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

static long s_exit_impl(int code) {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc != NULL) {
        curr_proc->state = PROC_ZOMBIE_STATE;
        curr_proc->exit_code = code;
        send_sigchld(curr_proc->pid);
    }    

    schedule_yield();
    return 0;
}

static long s_spawn(void* (*func)(void*), void *argv) {
    struct mem_ctx prev_ctx;
    mem_fetch_heap_vals(&prev_ctx);

    pid_t ppid = s_getpid();
    
    pid_t new_proc = proc_create(func, argv, ppid);
    if (new_proc < 0) {
        return new_proc;
    }
    pcb_t *new_pcb = get_pcb_by_pid(new_proc);
    if (new_pcb == NULL) {
        return -1;
    }

    mem_load_heap(&prev_ctx);
    return new_pcb->pid;
}

static long s_block_until_event(uint32_t event) {
    pcb_t *curr_proc = get_curr_process();
    if (curr_proc == NULL) {
        return -1;
    }

    curr_proc->blocked_until = event;
    block_process(curr_proc);

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
        schedule_yield();
        break;

    case S_EXIT:
        ret = s_exit_impl((int)frame->regs[0]);
        break;
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
    case S_KILL:
        ret = s_kill((pid_t)frame->regs[0], (int)(uintptr_t)frame->regs[1]);
        break;
    case S_BLOCK_UNTIL_EVENT:
        ret = s_block_until_event((uint32_t) frame->regs[0]);
        break;
    default:
        ret = SYS_ENOSYS;
        break;
    }

    frame->regs[0] = (uint64_t)ret;
    return frame;
}
