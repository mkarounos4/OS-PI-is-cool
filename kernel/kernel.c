#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "memory/kernel_mem.h"
#include "memory/malloc.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "syscall/u_syscall.h"

extern char __kernel_heap_start[];
extern char __kernel_heap_end[];

#define TRAP_TEST_FATAL_BRK 0
#define TRAP_TEST_FATAL_SVC 0
#define TRAP_TEST_UNDEFINED_INSTRUCTION 0
#define TRAP_TEST_DATA_ABORT 0
#define PROCESS_TESTS_ENABLED 1

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

struct process_test_args {
    const char *name;
    uint64_t delay_loops;
    int spawn_child;
};

static void *process_test_main(void *arg);

static struct process_test_args process_test_a_args = { "A", 250000u, 1 };
static struct process_test_args process_test_b_args = { "B", 350000u, 0 };
static struct process_test_args process_test_c_args = { "C", 450000u, 0 };
static struct process_test_args process_test_spawn_args = { "S", 550000u, 0 };

static uint64_t test_strlen(const char *s) {
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void user_puts(const char *s) {
    write_console(s, test_strlen(s));
}

static void user_puthex(uint64_t value) {
    char buf[18];
    int pos = 0;
    int started = 0;

    buf[pos++] = '0';
    buf[pos++] = 'x';

    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)((value >> shift) & 0xFu);
        if (digit != 0 || started || shift == 0) {
            started = 1;
            buf[pos++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        }
    }

    write_console(buf, (uint64_t)pos);
}

static void process_test_delay(uint64_t loops) {
    volatile uint64_t remaining = loops;
    while (remaining-- != 0) {
        asm volatile ("nop");
    }
}

static void *process_test_main(void *arg) {
    struct process_test_args *cfg = (struct process_test_args *)arg;
    long pid = getpid();
    void *heap_block = malloc(64);
    uint64_t heartbeat = 0;

    if (heap_block != NULL) {
        char *bytes = (char *)heap_block;
        bytes[0] = cfg->name[0];
        bytes[63] = '\0';
    }

    user_puts("[proc-test ");
    user_puts(cfg->name);
    user_puts("] start pid=");
    user_puthex((uint64_t)pid);
    user_puts(" heap=");
    user_puthex((uint64_t)(uintptr_t)heap_block);
    user_puts("\n");

    if (cfg->spawn_child) {
        long child_pid = spawn(process_test_main, &process_test_spawn_args);
        user_puts("[proc-test ");
        user_puts(cfg->name);
        user_puts("] spawned child pid=");
        user_puthex((uint64_t)child_pid);
        user_puts("\n");
    }

    while (1) {
        if (heap_block != NULL) {
            ((char *)heap_block)[1] = (char)heartbeat;
        }

        user_puts("[proc-test ");
        user_puts(cfg->name);
        user_puts("] heartbeat pid=");
        user_puthex((uint64_t)pid);
        user_puts(" n=");
        user_puthex(heartbeat++);
        user_puts(" heap=");
        user_puthex((uint64_t)(uintptr_t)heap_block);
        user_puts("\n");

        process_test_delay(cfg->delay_loops);
    }

    return NULL;
}

static void enqueue_process_or_report(const char *name, pid_t pid) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    uart_puts("[proc-test] ");
    uart_puts(name);
    uart_puts(" pid=");
    uart_puthex((uint64_t)pid);

    if (pcb == NULL) {
        uart_puts(" create FAILED\n");
        return;
    }

    add_task_to_scheduler(pcb);
    uart_puts(" queued\n");
}

static void run_process_self_tests(void) {
    uart_puts("[proc-test] creating infinite process scheduler tests\n");

    pid_t proc_a = proc_create(process_test_main, &process_test_a_args, -1);
    enqueue_process_or_report("A", proc_a);

    pid_t proc_b = proc_create(process_test_main, &process_test_b_args, -1);
    enqueue_process_or_report("B", proc_b);

    pid_t proc_c = proc_create(process_test_main, &process_test_c_args, proc_a);
    enqueue_process_or_report("C(child of A)", proc_c);

    pcb_t *a = get_pcb_by_pid(proc_a);
    pcb_t *b = get_pcb_by_pid(proc_b);
    pcb_t *c = get_pcb_by_pid(proc_c);

    if (a != NULL && b != NULL && c != NULL &&
        a->heap_ctx.heap_start != b->heap_ctx.heap_start &&
        b->heap_ctx.heap_start != c->heap_ctx.heap_start) {
        uart_puts("[proc-test] PASS distinct process heaps\n");
    } else {
        uart_puts("[proc-test] FAIL process heaps overlap or process missing\n");
    }

    if (a != NULL && vec_len(&a->children) > 0) {
        uart_puts("[proc-test] PASS parent child list updated\n");
    } else {
        uart_puts("[proc-test] FAIL parent child list missing child\n");
    }

    uart_puts("[proc-test] scheduler_start should now show A/B/C/S heartbeats forever\n");
}

void kernel_main(void) {
    uart_init();
    uart_puts("\nAArch64 bare-metal kernel entered\n");

    exceptions_init();
    run_trap_self_tests();

    irq_init();
    timer_init();
    irq_enable();

    kernel_mem_init(__kernel_heap_start, __kernel_heap_end);

    // Delay no scheduler test
    for (int i = 0; i < 10; i++) {
        uart_puthex(i);
        uart_puts(" test\n");
        timer_delay_ms(100);
    }

    scheduler_init();
#if PROCESS_TESTS_ENABLED
    run_process_self_tests();
#endif
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
