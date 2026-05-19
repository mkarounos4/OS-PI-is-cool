#include "scheduler.h"

#include <stdint.h>

#include "timer/timer.h"
#include "uart/uart.h"
#include "syscall/syscall.h"

#define SCHEDULER_QUANTUM_MS 1000u

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
static void thread1_func(uint64_t tid) {
    (void) tid;
    while (1) {
        sys_write_console("this is a thread heartbeat yeahhh\r\n", 35);
        uart_puts("done syscall\r\n");
        
        volatile uint64_t cycles = 1000000ULL;

        while (cycles--)
            __asm__ volatile ("nop");
    }
}

static void thread2_func(uint64_t tid) {
    (void) tid;
    while (1) {
        uart_puts("starting syscall 2\r\n");
        sys_write_console("nvm its not noooooo\r\n", 21);
        uart_puts("done syscall\r\n");
        volatile uint64_t cycles = 1000000ULL;

        while (cycles--)
            __asm__ volatile ("nop");
    }
}

// Initialize a new thread (make a tcb and load it for that index)
static void thread_init(void (*entry)(uint64_t)) {
    // Find next unused thread
    int tid = -1;
    for (unsigned int i = 0; i < THREAD_COUNT; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            tid = i;
            break;
        }
    }

    // If no unused threads, return with error
    if (tid < 0) {
        uart_puts("ERROR: thread limit reached.\n");
        return;
    }

    // Get stacks for thread
    uintptr_t kernel_top = align_down((uintptr_t)&threads[tid].kernel_stack[THREAD_STACK_SIZE], 16);
    uintptr_t user_top = align_down((uintptr_t)&threads[tid].usr_stack[THREAD_STACK_SIZE], 16);

    // Get trap_frame with thread context
    struct trap_frame *frame = (struct trap_frame *)(kernel_top - sizeof(struct trap_frame));

    // Initialize all thread registers to 0.
    for (unsigned i = 0; i < 31; i++) {
        frame->regs[i] = 0;
    }

    frame->regs[0] = (uint64_t)tid;

    // Initialize all special registers.
    frame->sp = user_top;
    frame->elr = (uint64_t)(uintptr_t)entry;
    frame->spsr = 0; // Initialize to SP_EL0 for user exception level
    frame->esr = 0;
    frame->far = 0;
    frame->type = 0;
    frame->intid = 0;

    // Initialize rest of tcb
    threads[tid].frame = frame;
    threads[tid].state = THREAD_READY;
    threads[tid].tid = tid;
}

// Initialize scheduler state and the initial 4 kernel threads.
void scheduler_init(void) {
    heartbeat = 0;
    curr_thread = 0;
    for (unsigned int i = 0; i < THREAD_COUNT; i++) {
        threads[i].state = THREAD_UNUSED;
    }

    thread_init(thread1_func);
    thread_init(thread2_func);
    thread_init(thread1_func);
}

// Starts execution at thread 0. Does not return.
void scheduler_start(void) {
    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);
    threads[0].state = THREAD_RUNNING;
    trap_frame_restore(threads[0].frame);
}

// Prints scheduler tick info to the console for debugging purposes.
static void scheduler_print_tick(unsigned int tid1, unsigned int tid2) {
    uart_puts("scheduler tick ");
    uart_puthex(heartbeat++);
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

    timer_schedule_interrupt_ms(SCHEDULER_QUANTUM_MS, scheduler_tick, 0);

    if (frame->type != EXC_IRQ_LOWER_A64) {
        return frame;
    }

    threads[curr_thread].frame = frame;
    threads[curr_thread].state = THREAD_READY;

    // Get next ready thread
    unsigned int start_thread = curr_thread;
    do {
        curr_thread = (curr_thread + 1) % THREAD_COUNT;
    } while (curr_thread != start_thread && threads[curr_thread].state != THREAD_READY);
    
    scheduler_print_tick(start_thread, curr_thread);

    // Verify there was a ready thread
    if (threads[curr_thread].state != THREAD_READY) {
        // need to go to idle task
        return frame;
    }

    // Update new thread data and start next thread
    threads[curr_thread].state = THREAD_RUNNING;
    return threads[curr_thread].frame;
}

