#pragma once

#include "traps/traps.h"
#include "scheduler/process.h"

#define THREAD_STACK_SIZE 4096u
#define THREAD_COUNT      4u

// Initializes scheduler state and the initial two kernel threads.
void scheduler_init(void);

// Starts execution at thread 0. Does not return.
void scheduler_start(void) __attribute__((noreturn));

void add_task_to_scheduler(pcb_t *pcb);

/* Threading */
void add_thread_to_scheduler(thread_t *thread, pcb_t *pcb);
thread_t *get_next_thread();

pcb_t *get_curr_process();

void scheduler_exit_current(int exit_code);

void schedule_yield();

void run_scheduler_if_needed();
