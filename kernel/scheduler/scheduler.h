#pragma once

#include "traps/traps.h"
#include "scheduler/process.h"

#define THREAD_STACK_SIZE 4096u
#define THREAD_COUNT      4u

// thread states
enum thread_state {
    THREAD_UNUSED,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_STOPPED,
    THREAD_ZOMBIE
};

// Thread control block structure
struct tcb_st {
    struct trap_frame *frame; // Trap frame with register state for this thread
    unsigned char usr_stack[THREAD_STACK_SIZE]; // Stack for this thread (EL0)
    unsigned char kernel_stack[THREAD_STACK_SIZE]; // kernel stack for this thread (EL1)
    uint32_t tid;
    enum thread_state state;
    uint32_t exit_code;

};

// Initializes scheduler state and the initial two kernel threads.
void scheduler_init(void);

// Starts execution at thread 0. Does not return.
void scheduler_start(void) __attribute__((noreturn));

void add_task_to_scheduler(pcb_t *pcb);

pcb_t *get_curr_process();

struct trap_frame *scheduler_exit_current(struct trap_frame *frame, int exit_code);

struct trap_frame *schedule_next_task();
