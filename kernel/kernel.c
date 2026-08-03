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
#include "gui/tty_gui_device.h"
#include "uart/uart_device.h"
#include "usb/xhci.h"
#include "usb/usb_keyboard_device.h"
#include "cpu/cpu.h"

#define FS_DEFAULT_INODE_TABLE_BLOCKS 64
#define FS_DEFAULT_BLOCK_SIZE_CONFIG 1
#define RAM_END_PHYS 0x40000000

static inline uint64_t read_mpidr_el1(void) {
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr;
}

void secondary_kernel_main(uint64_t cpu_id_value) {
    cpu_early_init((uint32_t)cpu_id_value, read_mpidr_el1());
    cpu_wait_until_started((uint32_t)cpu_id_value);
    install_kernel_page_table_cpu();
    exceptions_init();
    irq_init_cpu();
    timer_init_cpu();
    scheduler_cpu_init();
    cpu_mark_online((uint32_t)cpu_id_value);
    irq_enable();
    scheduler_start();
}

void kernel_main(void) {
    cpu_early_init(0, read_mpidr_el1());
    cpu_mark_online(0);
    uart_init();
#ifdef PLATFORM_RPI5
    uart_puts("[boot] build=rpi5 usb=rp1-xhci-hid\n");
#else
    uart_puts("[boot] build=qemu usb=disabled\n");
#endif
    printf("\nAArch64 bare-metal kernel entered\n");
    gui_framebuffer_init();
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
    cpu_prepare_secondary_cores();

    printf("cringe %d\n", -17);
    
    install_kernel_page_table();
    printf("[boot] final kernel page table installed\n");

    kmem_init((void *)(uintptr_t)KERNEL_HEAP_START,
              (void *)(uintptr_t)(KERNEL_HEAP_START + KERNEL_HEAP_SIZE));
    printf("[boot] kernel heap ready\n");
    init_tty_gui();
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
    err = uart_char_driver_init();
    if (err) {
        printf("[tty] ERROR: failed to init uart char driver\n");
    }
    err = usb_keyboard_char_driver_init();
    if (err) {
        printf("[usb] ERROR: failed to init keyboard char driver\n");
    }
    err = tty_gui_char_driver_init();
    if (err) {
        printf("[tty] ERROR: failed to init tty gui char driver\n");
    }
    err = tty_drivers_init();
    if (err) {
        printf("[tty] ERROR: failed to init tty driver\n");
    } else {
        printf("[tty] Initialized tty driver.");
    }
    err = uart_create_device_nodes();
    if (err) {
        printf("[tty] ERROR: failed to create uart device node\n");
    }
    err = usb_keyboard_create_device_nodes();
    if (err) {
        printf("[usb] ERROR: failed to create /dev/usb0\n");
    }
    err = tty_gui_create_device_nodes();
    if (err) {
        printf("[tty] ERROR: failed to create tty gui device nodes\n");
    }
    err = tty_create_device_nodes();
    if (err) {
        printf("[tty] ERROR: failed to create tty device nodes\n");
    }
    int tty = tty_create();
    if (tty < 0) {
        printf("[tty] ERROR: failed to create tty instance\n");
    } else {
        printf("[tty] Created terminal");
    }

#ifdef PLATFORM_RPI5
    if (xhci_keyboard_init() == 0) {
        printf("[usb] no xHCI controllers started; UART input remains active\n");
    }
#endif

    initialize_signals();

    printf("[boot] scheduler_init begin\n");
    scheduler_init();
    printf("[boot] scheduler_init done\n");
    cpu_release_secondary_cores();
    for (uint32_t spin = 0; spin < 1000000 && cpu_online_count() < MAX_CPUS; spin++) {
        asm volatile("yield" ::: "memory");
    }
    printf("[smp] online cpus=%d\n", cpu_online_count());
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        printf("while loop heartbeat\n");
    }
}
