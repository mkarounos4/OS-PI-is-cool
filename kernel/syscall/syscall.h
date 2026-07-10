#pragma once

#include <stdint.h>

#include "errno.h"
#include "traps/traps.h"
#include "devices/tty.h"

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
    S_MOUNT = 39,
    S_UNMOUNT = 40,
    S_PIPE = 41,
    S_PS = 42,
    S_EXEC = 43,
    S_GETCWD = 44,
    S_SLEEP = 45,
    S_STAT = 46,
};

struct trap_frame *syscall_dispatch(struct trap_frame *frame);
