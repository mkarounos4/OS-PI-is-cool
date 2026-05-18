#pragma once

#include "traps/traps.h"

#define THREAD_STACK_SIZE 4096u
#define THREAD_COUNT      2u

// Thread control block structure
struct tcb_st {
    struct trap_frame *frame; // Trap frame with register state for this thread
    unsigned char stack[THREAD_STACK_SIZE]; // Stack for this thread
};

// Initializes scheduler state and the initial two kernel threads.
void scheduler_init(void);

// Starts execution at thread 0. Does not return.
void scheduler_start(void) __attribute__((noreturn));
