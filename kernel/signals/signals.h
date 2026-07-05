#pragma once

#include <stdint.h>

#include "traps/traps.h"

#ifndef PID_T_DEFINED
#define PID_T_DEFINED
typedef int32_t pid_t;
#endif

enum signal_type {
    SIGINT = 2,
    SIGTTOU = 6,
    SIGTTIN = 7,
    SIGTSTP = 8,
    SIGKILL = 9,
    SIGSTOP = 10,
    SIGCONT = 11,
    SIGCHLD = 12,
    SIGTERM = 15,
};

typedef int signalset_t;

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIG_SET SIG_SETMASK

struct sigaction {
    void (*sa_handler)(int);
    signalset_t sa_mask;
    int sa_flags;
};

void SIG_IGN(int signum);
void SIG_DFL(int signum);
int s_kill(pid_t pid, int signal);
long send_sigchld(pid_t child);
int sigprocmask(int how, const signalset_t *set, signalset_t *oldset);
int sigemptyset(signalset_t *set);
int sigaddset(signalset_t *set, int signum);
int sigfillset(signalset_t *set);
int sigsuspend(const signalset_t *mask);
int sigaction(int signum, struct sigaction *sa, struct sigaction *old);
