#include "syscall/syscall.h"
#include "scheduler/process.h"

long write_console(const char *s, uint64_t len) {
    return sys_call2(S_WRITE_CONSOLE, (long)(uintptr_t)s, (long)len);
}

long putc(char c) {
    return sys_call1(S_PUTC, (long)c);
}

long get_ticks(void) {
    return sys_call0(S_GET_TICKS);
}

// NOT IMPLEMENTED YET
long delay(uint64_t ms) {
    (void) ms;
    return 0;
}

long exit(int code) {
    return sys_call1(S_EXIT, code);
}

long getpid(void) {
    return sys_call0(S_GETPID);
}

long spawn(void) {
    return 0;
}

long waitpid(pid_t pid, int *status, uint32_t flags) {
    return sys_call3(S_WAITPID, pid, (long) status, flags);
}

long sbrk(int64_t increment) {
    return sys_call1(S_SBRK, increment);
}