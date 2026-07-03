#include "scheduler.h"

#include <stdint.h>

#include "process.h"
#include "timer/timer.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "data-structs/vec.h"
#include "memory/page_table/page_table.h"
#include "memory/kmalloc.h"

#define SCHEDULER_QUANTUM_MS 1000u
#define PA_MASK UINT64_C(0x0000ffffffffffff)

static pcb_t *curr_proc;
static uint64_t curr_tick;

static struct cpu_context boot_ctx;
static struct cpu_context idle_ctx;
static volatile int ready_to_schedule = 0;
static void *ready_ctx = NULL;

static Vec pri_qs[3];
static int pri_counters[3] = {0, 0, 0};
static const int MAX_PRI_CNTRS[3] = {9, 6, 4};
static int curr_pri = 0;

static void scheduler_tick(void *ctx);

static void set_ready_to_schedule(void *ctx) {
    ready_to_schedule = 1;
    ready_ctx = ctx;
}

void add_task_to_scheduler(pcb_t *task) {
    vec_insert(&(pri_qs[task->priority]), 0, (ptr_t *) task);
}

// Runs 4 quantum on 2, 6 on 1, and 9 on 0
static pcb_t *get_next_task() {
    for (int i = 0; i < 3; i++) {
        if (!vec_is_empty(&(pri_qs[curr_pri]))) {
            ptr_t to_return;
            vec_pop_back(&(pri_qs[curr_pri]), &to_return);
            pcb_t *pcb = (pcb_t*) to_return;
            if (pcb->state != PROC_READY_STATE) {
                return get_next_task();
            }

            pri_counters[curr_pri]++;
            if (pri_counters[curr_pri] > MAX_PRI_CNTRS[curr_pri]) {
                pri_counters[curr_pri] = 0;
                curr_pri = (curr_pri+1) % 3;
            }
            return pcb;
        }

        curr_pri = (curr_pri+1) % 3;
    }

    return NULL;
}

void idle_task_fn(void* args) {
    (void)args;
    while (1) {
        asm volatile ("wfe");
    }
}

void scheduler_init(void) {
    curr_tick = 0;
    curr_proc = NULL;
    curr_pri = 0;

    for (int i = 0; i < 3; i++) {
        pri_qs[i] = vec_new(2, NULL);
        pri_counters[i] = 0;
    }

    processes_init();

    idle_ctx.x19 = 0;
    idle_ctx.x20 = 0;
    idle_ctx.x21 = 0;
    idle_ctx.x22 = 0;
    idle_ctx.x23 = 0;
    idle_ctx.x24 = 0;
    idle_ctx.x25 = 0;
    idle_ctx.x26 = 0;
    idle_ctx.x27 = 0;
    idle_ctx.x28 = 0;
    idle_ctx.x29 = 0;
    idle_ctx.x30 = (uint64_t)(uintptr_t)idle_task_fn;
    uint8_t *idle_stack = alloc_page();
    if (idle_stack == NULL) {
        uart_puts("ERROR: failed to allocate idle stack\n");
        return;
    }
    idle_ctx.sp = (uint64_t)(uintptr_t)(idle_stack + PAGE_SIZE);
    uint64_t *idle_l0 = initialize_user_page_table();
    if (idle_l0 == NULL) {
        uart_puts("ERROR: failed to initialize idle page table\n");
        return;
    }
    idle_ctx.ttbr0_el1 = kernel_phys_addr((uint64_t)(uintptr_t)idle_l0);
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);
    pcb_t *next_pcb = get_next_task();
    curr_proc = next_pcb;
    
    if (next_pcb != NULL) {
        next_pcb->state = PROC_RUNNING_STATE;
        context_switch(&boot_ctx, &next_pcb->ctx);
    } else {
        context_switch(&boot_ctx, &idle_ctx);
    }

    while (1) {
        asm volatile ("wfe");
    }
}

// Prints scheduler tick info to the console for debugging purposes.
static void __attribute__((unused)) scheduler_print_tick(unsigned int tid1, unsigned int tid2) {
    uart_puts("scheduler tick ");
    uart_puthex(curr_tick++);
    uart_puts(" switching ");
    uart_puthex(tid1);
    uart_puts(" -> ");
    uart_puthex(tid2);
    uart_puts(" ticks=");
    uart_puthex(timer_get_ticks());
    uart_puts("\n");
}

// Timer interrupt handler which performs actual scheduling
void scheduler_tick(void *ctx) {
    (void)ctx;

    struct cpu_context *old_ctx;
    struct cpu_context *new_ctx;

    // Get previous ctx
    if (curr_proc != NULL) {
        old_ctx = &curr_proc->ctx;
    } else {
        // if idle task, ignore ctx
        old_ctx = &idle_ctx;
    }

    if (curr_proc != NULL) {
        if (curr_proc->state == PROC_RUNNING_STATE) {
            curr_proc->state = PROC_READY_STATE;
            add_task_to_scheduler(curr_proc);
        }
    }

    // idle if no tasks
    curr_proc = get_next_task();
    // Load new proc heap to malloc
    if (curr_proc == NULL) {
        new_ctx = &idle_ctx;
    } else {
        new_ctx = &curr_proc->ctx;

        // Update new thread data
        curr_proc->state = PROC_RUNNING_STATE;
    }

    // If next thread exists, run it
    // scheduler_print_tick(old_pid, new_pid);
    
    // Setup next scheduler interrupt
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);

    // context switch to next process
    context_switch(old_ctx, new_ctx);

    // Handle queue'd signals
    if (curr_proc != NULL) {
        if ((curr_proc->pending_signals & (1 << SIGKILL)) &&
            curr_proc->sigactions[SIGKILL].sa_handler == SIG_DFL) {
            curr_proc->pending_signals &= ~(1 << SIGKILL);
            terminate_process(curr_proc);
        } else if ((curr_proc->pending_signals & (1 << SIGSTOP)) &&
                   curr_proc->sigactions[SIGSTOP].sa_handler == SIG_DFL) {
            curr_proc->pending_signals &= ~(1 << SIGSTOP);
            stop_process(curr_proc);
        }

        int curr = 0;
        while (curr_proc->pending_signals >> curr) {
            if ((curr_proc->pending_signals & (1 << curr)) && !(curr_proc->mask & (1 << curr))) {
                curr_proc->pending_signals &= ~(1 << curr);
                curr_proc->sigactions[curr].sa_handler(curr);
            }
            curr++;
        }
    }
}

pcb_t *get_curr_process() {
    return curr_proc;
}

void schedule_yield() {
    scheduler_tick(NULL);
}

void run_scheduler_if_needed() {
    if (!ready_to_schedule) {
        return;
    }

    ready_to_schedule = 0;
    scheduler_tick(ready_ctx);
}
