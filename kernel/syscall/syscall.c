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

struct trap_frame *syscall_dispatch(struct trap_frame *frame) {
    uint64_t syscall_number = frame->regs[8];
    long ret = SYS_ENOSYS;

    switch (syscall_number) {
    case SYS_WRITE_CONSOLE:
        ret = sys_write_console_impl((const char *)(uintptr_t)frame->regs[0], frame->regs[1]);
        break;

    case SYS_PUTC:
        uart_putc((char)frame->regs[0]);
        ret = 0;
        break;

    case SYS_GET_TICKS:
        ret = (long)timer_get_ticks();
        break;

    case SYS_YIELD:
        break;

    case SYS_EXIT:
    case SYS_GETPID:
    case SYS_CURRENT_EL:
    case SYS_DELAY:
        ret = SYS_ENOSYS;
        break;

    default:
        ret = SYS_ENOSYS;
        break;
    }

    frame->regs[0] = (uint64_t)ret;
    return frame;
}
