#include "scheduler.h"

#include <stdint.h>

#include "timer/timer.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "scheduler/process.h"
#include "data-structs/vec.h"
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

static struct tcb_st idle_task;

static struct trap_frame *scheduler_tick(struct trap_frame *frame, void *ctx);
extern void trap_frame_restore(struct trap_frame *frame) __attribute__((noreturn));

static uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

static void add_sched_queue_node(pcb_t *pcb) {
    struct sched_task_node *new_node = malloc(sizeof(struct sched_task_node));
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

    pcb_t *ret = sched_queue_head->pcb;
    free(task_node);
    sched_queue_size--;
    return ret;
}

void idle_task_fn(void*) {
    while (1) {
        asm volatile ("wfe");
    }
}

// Initialize a new thread (make a tcb and load it for that index)
static void thread_init(void (*entry)(void*), struct tcb_st *tcb) {
    // Get stacks for thread
    uintptr_t kernel_top = align_down((uintptr_t)&tcb->kernel_stack[THREAD_STACK_SIZE], 16);
    uintptr_t user_top = align_down((uintptr_t)&tcb->usr_stack[THREAD_STACK_SIZE], 16);

    // Get trap_frame with thread context
    struct trap_frame *frame = (struct trap_frame *)(kernel_top - sizeof(struct trap_frame));

    // Initialize all thread registers to 0.
    for (unsigned i = 0; i < 31; i++) {
        frame->regs[i] = 0;
    }

    // SET TID here temporary 0 as only using this for idle until real multithreaded library implemented
    frame->regs[0] = (uint64_t)0;

    // Initialize all special registers.
    frame->sp = user_top;
    frame->elr = (uint64_t)(uintptr_t)entry;
    frame->spsr = 0; // Initialize to SP_EL0 for user exception level
    frame->esr = 0;
    frame->far = 0;
    frame->type = 0;
    frame->intid = 0;

    // Initialize rest of tcb
    tcb->frame = frame;
    tcb->state = THREAD_READY;
    tcb->tid = 0;
}

void scheduler_init(void) {
    curr_tick = 0;
    curr_proc = NULL;
    processes_init();

    sched_queue_head = NULL;
    sched_queue_tail = NULL;
    sched_queue_size = 0;

    thread_init(idle_task_fn, &idle_task);
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);
    pcb_t *next_pcb = pop_sched_queue();
    
    if (next_pcb != NULL) {
        trap_frame_restore(next_pcb->frame);
    } else {
        trap_frame_restore(idle_task.frame);
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
static struct trap_frame *scheduler_tick(struct trap_frame *frame, void *ctx) {
    (void)ctx;

    if (frame->type != EXC_IRQ_LOWER_A64) {
        return frame;
    }

    if (curr_proc != NULL) {
        // TODO: load curr malloc brk back into curr_proc->heap_brk
    }

    // TODO: load kernel malloc __kernel_heap_start, __kernel_heap_end, and heap brk (store somewhere)

    if (curr_proc != NULL) {
        curr_proc->frame = frame;
        if (curr_proc->state == PROC_RUNNING_STATE) {
            curr_proc->state = PROC_READY_STATE;
            add_sched_queue_node(curr_proc);
        }
    }

    pid_t old_pcb = curr_proc == NULL ? -1 : curr_proc->pid;

    // idle if no tasks
    curr_proc = pop_sched_queue();
    if (curr_proc == NULL) {
        scheduler_print_tick(-1, -1);
        return idle_task.frame;
    }
    
    // If next thread exists, run it
    scheduler_print_tick(old_pcb, curr_proc->pid);

    // TODO: load malloc start,brk,end with curr_proc->head_[start/brk/end]
    // Also right before store back heap brk somewhere

    // Update new thread data
    curr_proc->state = PROC_RUNNING_STATE;
    
    // Setup next scheduler interrupt
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);

    // Return frame for new thread
    return curr_proc->frame;
}