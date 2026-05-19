#pragma once

#include <stdint.h>

#include "traps/traps.h"

enum syscall_type {
    SYS_WRITE_CONSOLE = 1,
    SYS_PUTC = 2,
    SYS_GET_TICKS = 3,
    SYS_YIELD = 4,
    SYS_EXIT = 5,
    SYS_GETPID = 6,
    SYS_CURRENT_EL = 7,
    SYS_DELAY = 8,
};

#define SYS_ENOSYS (-38L)
#define SYS_EFAULT (-14L)
#define SYS_EINVAL (-22L)

struct trap_frame *syscall_dispatch(struct trap_frame *frame);

static inline long sys_call6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x8 asm("x8") = nr;

    asm volatile(
        "svc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x8)
        :
        : "x6", "x7", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
          "x16", "x17", "x18", "x30", "memory", "cc");

    return x0;
}

static inline long sys_call0(long nr) {
    return sys_call6(nr, 0, 0, 0, 0, 0, 0);
}

static inline long sys_call1(long nr, long a0) {
    return sys_call6(nr, a0, 0, 0, 0, 0, 0);
}

static inline long sys_call2(long nr, long a0, long a1) {
    return sys_call6(nr, a0, a1, 0, 0, 0, 0);
}

static inline long sys_call3(long nr, long a0, long a1, long a2) {
    return sys_call6(nr, a0, a1, a2, 0, 0, 0);
}

static inline long sys_call4(long nr, long a0, long a1, long a2, long a3) {
    return sys_call6(nr, a0, a1, a2, a3, 0, 0);
}

static inline long sys_call5(long nr, long a0, long a1, long a2, long a3, long a4) {
    return sys_call6(nr, a0, a1, a2, a3, a4, 0);
}

static inline long sys_write_console(const char *s, uint64_t len) {
    return sys_call2(SYS_WRITE_CONSOLE, (long)(uintptr_t)s, (long)len);
}

static inline long sys_putc(char c) {
    return sys_call1(SYS_PUTC, (long)c);
}

static inline long sys_get_ticks(void) {
    return sys_call0(SYS_GET_TICKS);
}

// NOT IMPLEMENTED YET
static inline long sys_delay(uint64_t ms) {
    (void) ms;
    return 0;
}
