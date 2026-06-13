
#pragma once

#include <stdint.h>

enum syscall_type {
    S_WRITE_CONSOLE = 1,
    S_PUTC = 2,
    S_GET_TICKS = 3,
    S_YIELD = 4,
    S_EXIT = 5,
    S_GETPID = 6,
    S_CURRENT_EL = 7,
    S_DELAY = 8,
    S_SPAWN = 9,
    S_WAITPID = 10,
    S_SBRK = 11,
    S_KILL = 12,
    S_BLOCK_UNTIL_EVENT = 13,
};

long write_console(const char *s, uint64_t len);
long putc(char c);
long get_ticks(void);
long delay(uint64_t ms);
long exit(int code);
long getpid(void);
long spawn(void *(*func)(void *), void *arg);
long waitpid(pid_t pid, int *status, uint32_t flags);
long sbrk(int64_t increment);
long kill(pid_t pid, int signal);
long block_until_event(uint32_t events);

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
