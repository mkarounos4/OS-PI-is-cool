#pragma once

#include <stdint.h>

#include "syscall.h"

typedef int sig_atomic_t;
typedef int sigset_t;

#define SIGINT 2
#define SIGTTOU 6
#define SIGTTIN 7
#define SIGTSTP 8

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIG_SET SIG_SETMASK

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};

static inline int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return (int)sys_call3(S_SIGPROCMASK,
                          how,
                          (long)(uintptr_t)set,
                          (long)(uintptr_t)oldset);
}

static inline int sigemptyset(sigset_t *set) {
    return (int)sys_call1(S_SIGEMPTYSET, (long)(uintptr_t)set);
}

static inline int sigaddset(sigset_t *set, int signum) {
    return (int)sys_call2(S_SIGADDSET, (long)(uintptr_t)set, signum);
}

static inline int sigfillset(sigset_t *set) {
    return (int)sys_call1(S_SIGFILLSET, (long)(uintptr_t)set);
}

static inline int sigsuspend(const sigset_t *mask) {
    return (int)sys_call1(S_SIGSUSPEND, (long)(uintptr_t)mask);
}

static inline int sigaction(int signum, struct sigaction *sa, struct sigaction *old) {
    return (int)sys_call3(S_SIGACTION,
                          signum,
                          (long)(uintptr_t)sa,
                          (long)(uintptr_t)old);
}


#define WIFEXITED(X) ((X) == 1)
#define WIFSIGNALED(X) ((X) == 2)
#define WIFSTOPPED(X) ((X) == 3)
#define WIFCONTINUED(X) ((X) == 4)
