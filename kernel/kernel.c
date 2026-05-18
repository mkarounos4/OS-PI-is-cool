#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "uart/uart.h"

void kernel_main(void) {
    uart_init();
    uart_puts("\nAArch64 bare-metal kernel entered\n");

    exceptions_init();

    irq_init();
    scheduler_init();
    timer_init(1);
    irq_enable();

    // Delay no scheduler test
    for (int i = 0; i < 10; i++) {
        uart_puthex(i);
        uart_puts(" test\n");
        timer_delay_ms(100);
    }

    timer_enable_scheduler_tick();

    // Delay + scheduler interrupt test
    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
