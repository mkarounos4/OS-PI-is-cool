#pragma once

#include <stdint.h>

#include "traps/traps.h"

typedef int32_t pid_t;

enum signal_type {
    SIGINT = 2;
    SIGTTOU = 6;
    SIGTTIN = 7;
    SIGTSTP = 8;
    SIGKILL = 9,
    SIGSTOP = 10,
    SIGCONT = 11,
    SIGCHLD = 12,
    SIGTERM = 15,
};

typedef int sigset_t;

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};

int s_kill(pid_t pid, int signal);
int send_sigchld(pid_t child);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigemptyset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigfillset(sigset_t *set);
int sigsuspend(const sigset_t *mask);
