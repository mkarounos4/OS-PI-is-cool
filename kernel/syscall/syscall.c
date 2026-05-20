#include "syscall/syscall.h"

#include <stddef.h>

#include "timer/timer.h"
#include "traps/traps.h"
#include "uart/uart.h"

#define SYS_WRITE_CONSOLE_MAX 1024u
#define SYS_USER_PTR_MIN      UINT64_C(0x1000)

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
    (void) code;
    return 0;
}

static long s_spawn() {

    return 0;
}

static long s_waitpid() {
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
        ret = s_exit_impl((int)frame->regs[0]);
        break;
    case S_GETPID:
        break;
    case S_CURRENT_EL:
    case S_DELAY:
        ret = SYS_ENOSYS;
        break;
    case S_SPAWN:
        ret = s_spawn();
        break;
    case S_WAITPID:
        ret = s_waitpid();
        break;

    default:
        ret = SYS_ENOSYS;
        break;
    }

    frame->regs[0] = (uint64_t)ret;
    return frame;
}
