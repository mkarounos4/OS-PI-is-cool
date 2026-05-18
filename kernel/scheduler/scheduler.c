#include "scheduler.h"

#include <stdint.h>

#include "timer/timer.h"
#include "uart/uart.h"

#define SCHEDULER_QUANTUM_MS 1000u
#define SPSR_EL1H           0x5u

static struct tcb_st threads[THREAD_COUNT];
static unsigned curr_thread;
static uint64_t heartbeat;

static struct trap_frame *scheduler_tick(struct trap_frame *frame, void *ctx);
extern void trap_frame_restore(struct trap_frame *frame) __attribute__((noreturn));

// Aligns the given value down to the nearest multiple of the given alignment, which must be a power of 2.
static uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

// Entry point for thread1
static void thread1_func(void) {
    int curr_tick = 0;
    while (1) {
        uart_puts("thread 1 heartbeat ");
        uart_puthex(curr_tick++);
        uart_putc('\n');
        timer_delay_ms(500);
    }
}

// Entry point for thread2
static void thread_2_func(void) {
    int curr_tick = 0;
    while (1) {
        uart_puts("thread 2 heartbeat ");
        uart_puthex(curr_tick++);
        uart_putc('\n');
        timer_delay_ms(500);
    }
}

// Initialize a new thread (make a tcb and load it for that index)
static void thread_init(unsigned index, void (*entry)(void)) {
    uintptr_t stack_top =
        (uintptr_t)&threads[index].stack[THREAD_STACK_SIZE];
    stack_top = align_down(stack_top, 16u);

    struct trap_frame *frame =
        (struct trap_frame *)(stack_top - sizeof(struct trap_frame));

    for (unsigned i = 0; i < 31; i++) {
        frame->regs[i] = 0;
    }

    frame->sp = stack_top;
    frame->elr = (uint64_t)(uintptr_t)entry;
    frame->spsr = SPSR_EL1H;
    frame->esr = 0;
    frame->far = 0;
    frame->type = 0;
    frame->intid = 0;

    threads[index].frame = frame;
}

// Initialize scheduler state and the initial two kernel threads.
void scheduler_init(void) {
    heartbeat = 0;
    curr_thread = 0;

    thread_init(0, thread1_func);
    thread_init(1, thread_2_func);

    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    trap_frame_restore(threads[0].frame);
}

// Prints scheduler tick info to the console for debugging purposes.
static void scheduler_print_tick(void) {
    uart_puts("scheduler tick ");
    uart_puthex(heartbeat++);
    uart_puts(" switching ");
    uart_puthex(curr_thread);
    uart_puts(" -> ");
    uart_puthex(1u - curr_thread);
    uart_puts(" ticks=");
    uart_puthex(timer_get_ticks());
    uart_puts("\n");
}

// Timer interrupt handler which performs actual scheduling
static struct trap_frame *scheduler_tick(struct trap_frame *frame, void *ctx) {
    (void)ctx;

    threads[curr_thread].frame = frame;
    scheduler_print_tick();

    curr_thread = 1u - curr_thread;
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);

    return threads[curr_thread].frame;
}
