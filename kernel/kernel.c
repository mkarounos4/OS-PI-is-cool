#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "memory/kernel_mem.h"
#include "memory/malloc.h"
#include "memory/page_table/page_table.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "syscall/u_syscall.h"
#include "signals/signals.h"
#include "tests/tests.h"

extern char __kernel_heap_start[];
extern char __kernel_heap_end[];

void kernel_main(void) {
    uart_init();
    uart_puts("\nAArch64 bare-metal kernel entered\n");

    exceptions_init();

    uart_puts("[boot] irq_init begin\n");
    irq_init();
    uart_puts("[boot] irq_init done\n");
    uart_puts("[boot] timer_init begin\n");
    timer_init();
    uart_puts("[boot] timer_init done\n");
    uart_puts("[boot] timer frequency=");
    uart_puthex(timer_get_frequency());
    uart_puts("\n");
    irq_enable();
    uart_puts("[boot] irq_enable done\n");

    kernel_mem_init(__kernel_heap_start, __kernel_heap_end);
    uart_puts("[boot] kernel heap ready\n");
    uart_puts("[boot] virtual memory disabled for this test run\n");

    scheduler_init();
    
    // TESTS HERE
    // scheduler_orphan_test();
    waitpid_signal_test();

    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
