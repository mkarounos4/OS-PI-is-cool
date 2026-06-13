#include "scheduler.h"

#include <stdint.h>

#include "timer/timer.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "data-structs/vec.h"
#include "memory/page_table/page_table.h"
#include "memory/kernel_mem.h"
#include "memory/malloc.h"
#include "syscall/u_syscall.h"

#define SCHEDULER_QUANTUM_MS 1000u

struct sched_task_node {
    pcb_t *pcb;
    struct sched_task_node *next;
};
struct sched_task_node *sched_queue_head;
struct sched_task_node *sched_queue_tail;
int sched_queue_size;

static pcb_t *curr_proc;
static uint64_t curr_tick;

static struct cpu_context boot_ctx;
static struct cpu_context idle_ctx;
static volatile int ready_to_schedule = 0;
static void *ready_ctx = NULL;

static void scheduler_tick(void *ctx);

static void set_ready_to_schedule(void *ctx) {
    ready_to_schedule = 1;
    ready_ctx = ctx;
}


static void add_sched_queue_node(pcb_t *pcb) {
    if (pcb == NULL) {
        return;
    }

    struct sched_task_node *new_node = malloc(sizeof(struct sched_task_node));
    if (new_node == NULL) {
        uart_puts("ERROR: failed to allocate scheduler queue node\n");
        return;
    }

    new_node->pcb = pcb;
    new_node->next = NULL;

    if (sched_queue_tail == NULL) {
        sched_queue_head = new_node;
        sched_queue_tail = new_node;
    } else {
        sched_queue_tail->next = new_node;
        sched_queue_tail = new_node;
    }

    sched_queue_size++;
}

static pcb_t *pop_sched_queue() {
    if (sched_queue_size == 0) {
        return NULL;
    }

    struct sched_task_node *task_node = sched_queue_head;
    if (sched_queue_size == 1) {
        sched_queue_head = NULL;
        sched_queue_tail = NULL;
    } else {
        sched_queue_head = sched_queue_head->next;
    }

    pcb_t *ret = task_node->pcb;
    free(task_node);
    sched_queue_size--;
    if (ret->state != PROC_READY_STATE) {
        return pop_sched_queue();
    }
    return ret;
}

void idle_task_fn(void*) {
    while (1) {
        asm volatile ("wfe");
    }
}

void add_task_to_scheduler(pcb_t *pcb) {
    add_sched_queue_node(pcb);
}

void scheduler_init(void) {
    curr_tick = 0;
    curr_proc = NULL;

    sched_queue_head = NULL;
    sched_queue_tail = NULL;
    sched_queue_size = 0;

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
    idle_ctx.sp = 8192ul;
    idle_ctx.ttbr0_el1 = (uint64_t)(uintptr_t)initialize_user_page_table();
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);
    pcb_t *next_pcb = pop_sched_queue();
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
static void scheduler_print_tick(unsigned int tid1, unsigned int tid2) {
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
            add_sched_queue_node(curr_proc);
        }
    }

    pid_t old_pid = curr_proc == NULL ? -1 : curr_proc->pid;
    pid_t new_pid;

    // idle if no tasks
    curr_proc = pop_sched_queue();
    // Load new proc heap to malloc
    if (curr_proc == NULL) {
        new_ctx = &idle_ctx;
        new_pid = -1;
    } else {
        new_ctx = &curr_proc->ctx;

        // Update new thread data
        curr_proc->state = PROC_RUNNING_STATE;
        new_pid = curr_proc->pid;
    }

    // If next thread exists, run it
    scheduler_print_tick(old_pid, new_pid);
    
    // Setup next scheduler interrupt
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, 0);

    // context switch to next process
    context_switch(old_ctx, new_ctx);
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
