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
#include "cpu/cpu.h"
#include "sync/spinlock.h"

#define SCHEDULER_QUANTUM_MS 10u
#define PA_MASK UINT64_C(0x0000ffffffffffff)

static uint64_t curr_tick;

// priority queues for threads
static Vec thread_ready_queues[3];

static int pri_counters[3] = {0, 0, 0};
static const int MAX_PRI_CNTRS[3] = {9, 6, 4};
static int curr_pri = 0;
static spinlock_t scheduler_lock = SPINLOCK_INIT;
static volatile uint32_t boot_cpu_scheduler_started;

static void scheduler_tick(void *ctx);
static void add_thread_to_scheduler_locked(tcb_t *thread);
static tcb_t *get_next_thread_locked(void);

static void set_ready_to_schedule(void *ctx) {
    cpu_t *cpu = ctx != NULL ? (cpu_t *)ctx : cpu_current();
    if (cpu == NULL) {
        return;
    }
    cpu->ready_to_schedule = 1;
    cpu->ready_ctx = ctx;
}

void idle_task_fn(void* args) {
    (void)args;
    irq_enable();
    while (1) {
        asm volatile ("wfe");
    }
}

void scheduler_init(void) {
    curr_tick = 0;
    curr_pri = 0;
    boot_cpu_scheduler_started = 0;

    for (int i = 0; i < 3; i++) {
        thread_ready_queues[i] = vec_new(2, NULL);
        pri_counters[i] = 0;
    }

    threads_init();
    processes_init();
    scheduler_cpu_init();
}

void scheduler_cpu_init(void) {
    cpu_t *cpu = cpu_current();
    if (cpu == NULL) {
        return;
    }

    cpu->curr_thread = NULL;
    cpu->ready_to_schedule = 0;
    cpu->ready_ctx = NULL;

    cpu->idle_ctx.x19 = 0;
    cpu->idle_ctx.x20 = 0;
    cpu->idle_ctx.x21 = 0;
    cpu->idle_ctx.x22 = 0;
    cpu->idle_ctx.x23 = 0;
    cpu->idle_ctx.x24 = 0;
    cpu->idle_ctx.x25 = 0;
    cpu->idle_ctx.x26 = 0;
    cpu->idle_ctx.x27 = 0;
    cpu->idle_ctx.x28 = 0;
    cpu->idle_ctx.x29 = 0;
    cpu->idle_ctx.x30 = (uint64_t)(uintptr_t)idle_task_fn;
    uint8_t *idle_stack = alloc_page();
    if (idle_stack == NULL) {
        uart_puts("ERROR: failed to allocate idle stack\n");
        return;
    }
    cpu->idle_stack = idle_stack;
    cpu->idle_ctx.sp = (uint64_t)(uintptr_t)(idle_stack + PAGE_SIZE);
    uint64_t *idle_l0 = initialize_user_page_table();
    if (idle_l0 == NULL) {
        uart_puts("ERROR: failed to initialize idle page table\n");
        return;
    }
    cpu->idle_ctx.ttbr0_el1 = kernel_phys_addr((uint64_t)(uintptr_t)idle_l0);
    cpu->idle_ctx.ttbr0_el1_va = (uint64_t)(uintptr_t)idle_l0;
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    cpu_t *cpu = cpu_current();
    if (cpu == NULL) {
        while (1) {
            asm volatile("wfe" ::: "memory");
        }
    }

    if (cpu->id != 0) {
        while (!boot_cpu_scheduler_started) {
            asm volatile("wfe" ::: "memory");
        }
        asm volatile("dmb sy" ::: "memory");
    }

    uint64_t flags = spin_lock_irqsave(&scheduler_lock);
    timer_schedule_local_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, cpu);
    tcb_t *next_thread = get_next_thread_locked();
    
    if (next_thread != NULL) {
        cpu->curr_thread = next_thread;
        next_thread->state = THREAD_RUNNING;
        if (cpu->id == 0) {
            boot_cpu_scheduler_started = 1;
            asm volatile("dmb sy\nsev" ::: "memory");
        }
        context_switch_unlock_irqrestore(&cpu->boot_ctx, &next_thread->ctx,
                                         &scheduler_lock, flags);
    } else {
        if (cpu->id == 0) {
            boot_cpu_scheduler_started = 1;
            asm volatile("dmb sy\nsev" ::: "memory");
        }
        context_switch_unlock_irqrestore(&cpu->boot_ctx, &cpu->idle_ctx,
                                         &scheduler_lock, flags);
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

static void scheduler_handle_pending_signals(void) {
    tcb_t *curr_thread = get_curr_thread();
    if (curr_thread != NULL) {
        // Handle queue'd process level signals
        pcb_t *curr_proc = curr_thread->pcb;
        if ((curr_proc->pending_signals & (1 << SIGKILL)) &&
            curr_proc->sigactions[SIGKILL].sa_handler == SIG_DFL) {
            curr_proc->pending_signals &= ~(1 << SIGKILL);
            terminate_thread(curr_thread);
        } else if ((curr_proc->pending_signals & (1 << SIGSTOP)) &&
                   curr_proc->sigactions[SIGSTOP].sa_handler == SIG_DFL) {
            curr_proc->pending_signals &= ~(1 << SIGSTOP);
            stop_thread(curr_thread);
        }

        int curr = 0;
        while (curr_proc->pending_signals >> curr) {
            if ((curr_proc->pending_signals & (1 << curr)) && !(curr_thread->mask & (1 << curr))) {
                void (*handler)(int) = curr_proc->sigactions[curr].sa_handler;
                curr_proc->pending_signals &= ~(1 << curr);
                if (handler == SIG_DFL || handler == SIG_IGN) {
                    handler(curr);
                } else {
                    user_def_sig_handler(curr);
                }
            }
            curr++;
        }
    }

    if (curr_thread != NULL) {
        pcb_t *curr_proc = curr_thread->pcb;
        // Handle queue'd thread level signals
        if ((curr_thread->pending_signals & (1 << SIGKILL)) &&
            curr_proc->sigactions[SIGKILL].sa_handler == SIG_DFL) {
            curr_thread->pending_signals &= ~(1 << SIGKILL);
            terminate_thread(curr_thread);
        } else if ((curr_thread->pending_signals & (1 << SIGSTOP)) &&
                   curr_proc->sigactions[SIGSTOP].sa_handler == SIG_DFL) {
            curr_thread->pending_signals &= ~(1 << SIGSTOP);
            stop_thread(curr_thread);
        }

        int curr = 0;
        while (curr_thread->pending_signals >> curr) {
            if ((curr_thread->pending_signals & (1 << curr)) && !(curr_thread->mask & (1 << curr))) {
                void (*handler)(int) = curr_proc->sigactions[curr].sa_handler;
                curr_thread->pending_signals &= ~(1 << curr);
                if (handler == SIG_DFL || handler == SIG_IGN) {
                    handler(curr);
                } else {
                    user_def_sig_handler(curr);
                }
            }
            curr++;
        }
    }
}

// Timer interrupt handler which performs actual scheduling
void scheduler_tick(void *ctx) {
    (void)ctx;
    cpu_t *cpu = cpu_current();
    if (cpu == NULL) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&scheduler_lock);
    tcb_t *curr_thread = cpu->curr_thread;
    cpu->scheduler_ticks++;
    
    // Save current thread context
    if (curr_thread != NULL) {
        if (curr_thread->state == THREAD_RUNNING) {
            curr_thread->state = THREAD_READY;
            add_thread_to_scheduler_locked(curr_thread);
        }
    }

    // Get next thread from any process
    tcb_t *next_thread = get_next_thread_locked();

    if (next_thread == NULL) {
        struct cpu_context *old_ctx = curr_thread ? &curr_thread->ctx : &cpu->idle_ctx;
        cpu->curr_thread = NULL;
        timer_schedule_local_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, cpu);
        context_switch_unlock_irqrestore(old_ctx, &cpu->idle_ctx,
                                         &scheduler_lock, flags);
        scheduler_handle_pending_signals();
        return;
    }

    struct cpu_context *old_ctx = curr_thread ? &curr_thread->ctx : &cpu->idle_ctx;
    cpu->curr_thread = next_thread;
    next_thread->state = THREAD_RUNNING;

    timer_schedule_local_interrupt_ms(SCHEDULER_QUANTUM_MS, set_ready_to_schedule, cpu);
    context_switch_unlock_irqrestore(old_ctx, &next_thread->ctx,
                                     &scheduler_lock, flags);

    scheduler_handle_pending_signals();
}

pcb_t *get_curr_process(void) {
    tcb_t *curr_thread = get_curr_thread();
    if (curr_thread == NULL) {
        return NULL;
    }

    return curr_thread->pcb;
}

tcb_t *get_curr_thread(void) {
    cpu_t *cpu = cpu_current();
    return cpu != NULL ? cpu->curr_thread : NULL;
}

static void add_thread_to_scheduler_locked(tcb_t *thread) {
    if (thread == NULL || thread->state != THREAD_READY) {
        return;
    }

    for (int priority = 0; priority < 3; priority++) {
        for (size_t i = 0; i < vec_len(&thread_ready_queues[priority]); i++) {
            if (vec_get(&thread_ready_queues[priority], i) == thread) {
                return;
            }
        }
    }

    vec_insert(&thread_ready_queues[thread->priority], 0, (ptr_t*)thread);
}

void add_thread_to_scheduler(tcb_t *thread) {
    uint64_t flags = spin_lock_irqsave(&scheduler_lock);
    add_thread_to_scheduler_locked(thread);
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

void remove_thread_from_scheduler(tcb_t *thread) {
    if (thread == NULL) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&scheduler_lock);
    for (int priority = 0; priority < 3; priority++) {
        size_t i = 0;
        while (i < vec_len(&thread_ready_queues[priority])) {
            if (vec_get(&thread_ready_queues[priority], i) == thread) {
                vec_erase(&thread_ready_queues[priority], i);
            } else {
                i++;
            }
        }
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

static tcb_t *get_next_thread_locked(void) {
    for (int priorities_checked = 0; priorities_checked < 3; priorities_checked++) {
        while (!vec_is_empty(&thread_ready_queues[curr_pri])) {
            ptr_t to_return;
            vec_pop_back(&thread_ready_queues[curr_pri], &to_return);
            tcb_t *thread = (tcb_t *)to_return;

            if (thread == NULL || thread->state != THREAD_READY) {
                continue;
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

tcb_t *get_next_thread(void) {
    uint64_t flags = spin_lock_irqsave(&scheduler_lock);
    tcb_t *thread = get_next_thread_locked();
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return thread;
}

void schedule_yield() {
    scheduler_tick(NULL);
}

void run_scheduler_if_needed() {
    cpu_t *cpu = cpu_current();
    if (cpu == NULL || !cpu->ready_to_schedule) {
        return;
    }

    cpu->ready_to_schedule = 0;
    scheduler_tick(cpu->ready_ctx);
}
