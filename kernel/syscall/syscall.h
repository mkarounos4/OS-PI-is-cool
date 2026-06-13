#pragma once

#include <stdint.h>

#include "traps/traps.h"

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

#define SYS_ENOSYS (-38L)
#define SYS_EFAULT (-14L)
#define SYS_EINVAL (-22L)

struct trap_frame *syscall_dispatch(struct trap_frame *frame);
