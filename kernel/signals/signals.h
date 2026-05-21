#pragma once

#include <stdint.h>

#include "traps/traps.h"

typedef int32_t pid_t;

enum signal_type {
    SIGKILL = 9,
    SIGSTOP = 10,
    SIGTERM = 11,
    SIGCONT = 12,
    SIGCHLD = 13,
};


long s_kill(pid_t pid, int signal);
long send_sigchld(pid_t child);
