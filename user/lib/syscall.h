#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errno.h"

typedef int32_t pid_t;
typedef void *ptr_t;

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 4

#define ECHILD_NEG (-ECHILD)

#define BLOCK_UNTIL_NEW_CHILD 1
#define BLOCK_UNTIL_TTY_REQUEST 8

#define SIGKILL 9
#define SIGSTOP 10
#define SIGCONT 11
#define SIGCHLD 12
#define SIGTERM 15

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
    S_FS_TOUCH = 14,
    S_FS_MV = 15,
    S_FS_RM = 16,
    S_FS_CAT = 17,
    S_FS_CP = 18,
    S_FS_CHMOD = 19,
    S_FS_LS = 20,
    S_FS_MKDIR = 21,
    S_FS_CD = 22,
    S_FS_OPEN = 23,
    S_FS_CLOSE = 24,
    S_FS_LSEEK = 25,
    S_FS_READ = 26,
    S_FS_WRITE = 27,
    S_SIGPROCMASK = 28,
    S_SIGEMPTYSET = 29,
    S_SIGADDSET = 30,
    S_SIGFILLSET = 31,
    S_SIGSUSPEND = 32,
    S_SIGACTION = 33,
    S_FORK = 34,
    S_DUP2 = 35,
    S_SETPGID = 36,
    S_GETPGRP = 37,
    S_TCSETPGRP = 38,
    S_FS_MOUNT = 39,
    S_FS_UNMOUNT = 40,
    S_PIPE = 41,
    S_PS = 42,
    S_EXEC = 43,
    S_GETCWD = 44,
    S_SLEEP = 45,
    S_STAT = 46,
    S_TTY_NEXT_REQUEST = 47,
};

long write_console(const char *s, uint64_t len);
long putc(char c);
long get_ticks(void);
long delay(uint64_t ms);
long sleep(uint64_t ms);
long exit(int code);
long getpid(void);
long spawn(void *(*func)(void *), void *arg);
long waitpid(pid_t pid, int *status, uint32_t flags);
long sbrk(uint64_t old_brk, uint64_t new_brk);
long kill(pid_t pid, int signal);
long block_until_event(uint32_t events);
long tty_next_request(void);
void putstr(const char *s);
void puthex(uint64_t value);

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

static inline pid_t setpgid(pid_t pid, pid_t pgid) {
    return sys_call2(S_SETPGID, pid, pgid);
}

static inline pid_t getpgrp(void) {
    return sys_call0(S_GETPGRP);
}

static inline int tcsetpgrp(int fd, pid_t pgrp) {
    return sys_call2(S_TCSETPGRP, fd, pgrp);
}

static inline int fork(void) {
    return sys_call0(S_FORK);
}

static inline int dup2(int oldfd, int newfd) {
    return sys_call2(S_DUP2, oldfd, newfd);
}

static inline int mount() {
    return 0;
}

static inline int unmount() {
    return -ENOSYS;
}

static inline int pipe(int pipefd[2]) {
    return sys_call1(S_PIPE, (long) pipefd);
}

static inline int ps(void) {
    return sys_call0(S_PS);
}

static inline int exec(const char *path, char *const argv[]) {
    return sys_call2(S_EXEC, (long)(uintptr_t)path, (long)(uintptr_t)argv);
}
