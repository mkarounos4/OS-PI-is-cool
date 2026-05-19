#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "memory/malloc.h"
#include "uart/uart.h"
#include "syscall/syscall.h"

#define TRAP_TEST_FATAL_BRK 0
#define TRAP_TEST_FATAL_SVC 0
#define TRAP_TEST_UNDEFINED_INSTRUCTION 0
#define TRAP_TEST_DATA_ABORT 0

static void run_trap_self_tests(void) {
    uart_puts("[trap-test] current EL before scheduler: ");
    uart_puthex(cpu_current_el());
    uart_putc('\n');

    uart_puts("[trap-test] BRK self-test begin\n");
    asm volatile("brk #0x42");
    uart_puts("[trap-test] BRK self-test returned\n");

#if TRAP_TEST_FATAL_BRK
    uart_puts("[trap-test] fatal BRK begin; this should not return\n");
    asm volatile("brk #0x43");
    uart_puts("[trap-test] ERROR: fatal BRK returned\n");
#endif

#if TRAP_TEST_FATAL_SVC
    uart_puts("[trap-test] SVC begin; this should halt until syscall dispatch exists\n");
    asm volatile("svc #0");
    uart_puts("[trap-test] ERROR: SVC returned\n");
#endif

#if TRAP_TEST_UNDEFINED_INSTRUCTION
    uart_puts("[trap-test] undefined instruction begin; this should not return\n");
    asm volatile(".inst 0x00000000");
    uart_puts("[trap-test] ERROR: undefined instruction returned\n");
#endif

#if TRAP_TEST_DATA_ABORT
    uart_puts("[trap-test] data abort begin; this is only reliable after MMU/page tables are enabled\n");
    *(volatile uint64_t *)UINT64_C(0xfffffffffffff000) = UINT64_C(0x1234);
    uart_puts("[trap-test] ERROR: data abort test returned\n");
#endif
}

void kernel_main(void) {
    uart_init();
    uart_puts("\nAArch64 bare-metal kernel entered\n");

    exceptions_init();
    run_trap_self_tests();

    irq_init();
    timer_init();
    irq_enable();

    // Delay no scheduler test
    for (int i = 0; i < 10; i++) {
        uart_puthex(i);
        uart_puts(" test\n");
        timer_delay_ms(100);
    }

    scheduler_init();
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
