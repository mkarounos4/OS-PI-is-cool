#include "scheduler.h"

#include <stdint.h>

#include "timer/timer.h"
#include "uart/uart.h"

static uint64_t heartbeat;

void scheduler_init(void) {
    heartbeat = 0;
}

void scheduler_print_tick(struct trap_frame *frame) {
    (void)frame;

    uart_puts("scheduler tick ");
    uart_puthex(heartbeat++);
    uart_puts(" ticks=");
    uart_puthex(timer_get_ticks());
    uart_puts("\n");
}

// Scheduler tick function called by timer interrupt handler every quantum.
void scheduler_tick(struct trap_frame *frame) {
    scheduler_print_tick(frame);
}
