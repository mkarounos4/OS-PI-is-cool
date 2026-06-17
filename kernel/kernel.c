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
#include "string.h"
#include "fs/disk.h"
#include "fs/errors.h"
#include "fs/fs_test.h"
#include "fs/cmds.h"
#include "fs/kapi.h"
#include "fan/fan.h"

#define FS_DEFAULT_INODE_TABLE_BLOCKS 4
#define FS_DEFAULT_BLOCK_SIZE_CONFIG 1

void kernel_main(void) {
    uart_init();
    uart_puts("\nAArch64 bare-metal kernel entered\n");
    fan_init();

    exceptions_init();

    uart_puts("[boot] irq_init begin\n");
    irq_init();
    uart_puts("[boot] irq_init done\n");
    uart_irq_init();
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

    int block_ready = 0;
    uart_puts("[boot] block_init begin\n");
    if (block_init() == 0) {
        block_ready = 1;
        uart_puts("[boot] block_init done\n");
    } else {
        uart_puts("[boot] block_init failed\n");
    }

    int fs_ready = 0;
    int err = SUCCESS;
    if (block_ready) {
        err = mount();
        if (err == FS_INVALID) {
            uart_puts("[fs] invalid fs, running mkfs\n");
            err = mkfs(FS_DEFAULT_INODE_TABLE_BLOCKS,
                       FS_DEFAULT_BLOCK_SIZE_CONFIG);
            if (err != SUCCESS) {
                uart_puts("[fs] ERROR: failed to mkfs\n");
                print_error(err);
            } else {
                uart_puts("[fs] mkfs done, mounting\n");
                err = mount();
            }
        }

        if (err == SUCCESS) {
            fs_ready = 1;
            uart_puts("[fs] mounted fs\n");
        } else {
            uart_puts("[fs] ERROR: failed to mount fs\n");
            print_error(err);
        }
    }

    initialize_char_device_registry();
    uart_puts("[tty] Intialized char device registry.");
    err = tty_drivers_init();
    if (err) {
        uart_puts("[tty] ERROR: failed to init tty driver\n");
    } else {
        uart_puts("[tty] Initialized tty driver.");
    }
    int tty = tty_create();
    if (tty < 0) {
        uart_puts("[tty] ERROR: failed to create tty instance\n");
    } else {
        uart_puts("[tty] Created terminal");
    }

    scheduler_init();
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
