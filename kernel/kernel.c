#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "memory/kmalloc.h"
#include "memory/page_table/page_table.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "signals/signals.h"
#include "memory/mmu.h"
#include "disk/block.h"
#include "disk/block_test.h"

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

    install_kernel_page_table();
    uart_puts("[boot] final kernel page table installed\n");

    kmem_init((void *)(uintptr_t)KERNEL_HEAP_START,
              (void *)(uintptr_t)(KERNEL_HEAP_START + KERNEL_HEAP_SIZE));
    uart_puts("[boot] kernel heap ready\n");
    uart_puts("[boot] virtual memory enabled\n");

#if defined(PLATFORM_RPI5) || defined(PLATFORM_QEMU)
    uart_puts("[boot] block_init begin\n");
    if (block_init() == 0) {
        uart_puts("[boot] block_init done\n");
        // WARNING: this writes exactly one sector. Pick an LBA that is not used by a partition/filesystem.
        // First boot: write and immediately read back.
        // block_test_write_read(UINT64_C(1048576));
        // Second boot after power-off/unplug/replug: verify the same sector persisted.
        block_test_verify_persistence(UINT64_C(1048576));
        // Milestone 2: multi-block write/read starting at the same scratch range.
        block_test_multi_write_read(UINT64_C(1048576));
    } else {
        uart_puts("[boot] block_init failed\n");
    }
#endif

    scheduler_init();
    
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
