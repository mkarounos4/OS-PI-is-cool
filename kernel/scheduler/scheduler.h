#pragma once

#include "traps/traps.h"
#include "scheduler/process.h"

#define THREAD_STACK_SIZE 4096u
#define THREAD_COUNT      4u

// Initializes scheduler state and the initial two kernel threads.
void scheduler_init(void);

// Starts execution at thread 0. Does not return.
void scheduler_start(void) __attribute__((noreturn));

/* Threading */
void add_thread_to_scheduler(tcb_t *thread);
void remove_thread_from_scheduler(tcb_t *thread);
tcb_t *get_next_thread(void);

pcb_t *get_curr_process();
tcb_t *get_curr_thread(void);

void scheduler_exit_current(int exit_code);

void schedule_yield();

void run_scheduler_if_needed();
