#include "scheduler.h"

#include <stdint.h>

#include "process.h"
#include "timer/timer.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "data-structs/vec.h"
#include "memory/page_table/page_table.h"
#include "memory/kmalloc.h"
#include "traps/traps.h"

#define SCHEDULER_QUANTUM_MS 10u
#define PA_MASK UINT64_C(0x0000ffffffffffff)

static pcb_t *curr_proc;
static uint64_t curr_tick;

static thread_t *curr_thread;
// priority queues for threads
static Vec thread_ready_queues[3];

static struct cpu_context boot_ctx;
static struct cpu_context idle_ctx;
static volatile int ready_to_schedule = 0;
static void *ready_ctx = NULL;

static int pri_counters[3] = {0, 0, 0};
static const int MAX_PRI_CNTRS[3] = {9, 6, 4};
static int curr_pri = 0;

static void scheduler_tick(void *ctx);

static void set_ready_to_schedule(void *ctx) {
    ready_to_schedule = 1;
    ready_ctx = ctx;
}

void add_task_to_scheduler(pcb_t *task) {
    if (task == NULL || task->state == PROC_UNUSED_STATE ||
        task->state == PROC_ZOMBIE_STATE ||
        task->state == PROC_STOPPED_STATE ||
        task->state == PROC_BLOCKED_STATE) {
        return;
    }

    for (int i = 0; i < task->thread_count; i++) {
        if (task->threads[i].state == THREAD_READY) {
            add_thread_to_scheduler(&task->threads[i], task);
        }
    }
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
    curr_thread = NULL;
    curr_pri = 0;

    for (int i = 0; i < 3; i++) {
        thread_ready_queues[i] = vec_new(2, NULL);
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
    thread_t *next_thread = get_next_thread();
    
    if (next_thread != NULL) {
        curr_thread = next_thread;
        curr_proc = next_thread->pcb;
        curr_thread->state = THREAD_RUNNING;
        curr_proc->state = PROC_RUNNING_STATE;
        context_switch(&boot_ctx, &next_thread->ctx);
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
    
    // Save current thread context
    if (curr_thread != NULL) {
        if (curr_thread->state == THREAD_RUNNING) {
            curr_thread->state = THREAD_READY;
            if (curr_thread->pcb != NULL &&
                curr_thread->pcb->state == PROC_RUNNING_STATE) {
                curr_thread->pcb->state = PROC_READY_STATE;
            }
            add_thread_to_scheduler(curr_thread, curr_thread->pcb);
        }
    }

    // Get next thread from any process
    thread_t *next_thread = get_next_thread();

    if (next_thread == NULL) {
        struct cpu_context *old_ctx = curr_thread ? &curr_thread->ctx : &idle_ctx;
        curr_thread = NULL;
        curr_proc = NULL;
        timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);
        context_switch(old_ctx, &idle_ctx);
        return;
    }

    struct cpu_context *old_ctx = curr_thread ? &curr_thread->ctx : &idle_ctx;
    curr_thread = next_thread;
    curr_proc = next_thread->pcb;
    next_thread->state = THREAD_RUNNING;
    if (curr_proc != NULL) {
        curr_proc->state = PROC_RUNNING_STATE;
    }

    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);
    context_switch(old_ctx, &next_thread->ctx);
}

pcb_t *get_curr_process() {
    return curr_proc;
}

thread_t *get_curr_thread(void) {
    return curr_thread;
}

void add_thread_to_scheduler(thread_t *thread, pcb_t *pcb) {
    if (thread == NULL) {
        return;
    }
    thread->pcb = pcb;
    vec_insert(&thread_ready_queues[pcb->priority], 0, (ptr_t*)thread);
}

thread_t *get_next_thread(void) {
    for (int i = 0; i < 3; i++) {
        if (!vec_is_empty(&thread_ready_queues[curr_pri])) {
            ptr_t to_return;
            vec_pop_back(&thread_ready_queues[curr_pri], &to_return);
            thread_t *thread = (thread_t*)to_return;

            if (thread->state != THREAD_READY) {
                return get_next_thread();
            }

            pri_counters[curr_pri]++;
            if (pri_counters[curr_pri] > MAX_PRI_CNTRS[curr_pri]) {
                pri_counters[curr_pri] = 0;
                curr_pri = (curr_pri + 1) % 3;
            }
            return thread;
        }
        curr_pri = (curr_pri + 1) % 3;
    }
    return NULL;
}

void schedule_yield() {
    irq_enable();
    scheduler_tick(NULL);
}

void run_scheduler_if_needed() {
    if (!ready_to_schedule) {
        return;
    }

    ready_to_schedule = 0;
    scheduler_tick(ready_ctx);
}
