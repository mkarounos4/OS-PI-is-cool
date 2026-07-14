#include <stdint.h>

#include "gui/tty_gui.h"
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
#include "gui/gui.h"

#define FS_DEFAULT_INODE_TABLE_BLOCKS 8
#define FS_DEFAULT_BLOCK_SIZE_CONFIG 1
#define RAM_END_PHYS 0x40000000

void kernel_main(void) {
    uart_init();
    printf("\nAArch64 bare-metal kernel entered\n");
    gui_framebuffer_init();
    init_tty_gui();
    fan_init();

    exceptions_init();

    printf("[boot] irq_init begin\n");
    irq_init();
    printf("[boot] irq_init done\n");
    uart_irq_init();
    printf("[boot] timer_init begin\n");
    timer_init();
    printf("[boot] timer_init done\n");
    printf("[boot] timer frequency=");
    printf("%x", timer_get_frequency());
    printf("\n");
    irq_enable();
    printf("[boot] irq_enable done\n");

    printf("cringe %d\n", -17);
    
    install_kernel_page_table();
    printf("[boot] final kernel page table installed\n");

    kmem_init((void *)(uintptr_t)KERNEL_HEAP_START,
              (void *)(uintptr_t)(KERNEL_HEAP_START + KERNEL_HEAP_SIZE));
    printf("[boot] kernel heap ready\n");
    struct Page *pages = kmalloc(RAM_END_PHYS / PAGE_SIZE);
    pt_init(pages);
    printf("[boot] virtual memory enabled\n");

    int block_ready = 0;
    printf("[boot] block_init begin\n");
    if (block_init() == 0) {
        block_ready = 1;
        printf("[boot] block_init done\n");
    } else {
        printf("[boot] block_init failed\n");
    }

    int err = SUCCESS;
    if (block_ready) {
        err = mount();
        if (err == FS_INVALID) {
            printf("[fs] invalid fs, running mkfs\n");
            err = mkfs(FS_DEFAULT_INODE_TABLE_BLOCKS,
                       FS_DEFAULT_BLOCK_SIZE_CONFIG);
            if (err != SUCCESS) {
                printf("[fs] ERROR: failed to mkfs\n");
                print_error(err);
            } else {
                printf("[fs] mkfs done, mounting\n");
                err = mount();
            }
        }

        if (err == SUCCESS) {
            printf("[fs] mounted fs\n");
        } else {
            printf("[fs] ERROR: failed to mount fs\n");
            print_error(err);
        }
    }

    initialize_char_device_registry();
    printf("[tty] Intialized char device registry.");
    err = tty_drivers_init();
    if (err) {
        printf("[tty] ERROR: failed to init tty driver\n");
    } else {
        printf("[tty] Initialized tty driver.");
    }
    for (int i = 0; i < 2; i++) {
        int tty = tty_create();
        if (tty < 0) {
            printf("[tty] ERROR: failed to create tty instance\n");
        } else {
            printf("[tty] Created terminal");
        }
    }

    scheduler_init();
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        printf("while loop heartbeat\n");
    }
}
