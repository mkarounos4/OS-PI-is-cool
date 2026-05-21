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
#define PROCESS_TEST_MODE_TERMINATING 0

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

struct terminating_process_args {
    const char *name;
    uint64_t delay_loops;
    uint64_t heartbeats;
    int exit_code;
};

static void *explicit_exit_process_main(void *arg);
static void *implicit_exit_process_main(void *arg);
static void *spawning_exit_process_main(void *arg);

static struct terminating_process_args explicit_exit_args = {
    "explicit-exit",
    180000u,
    2u,
    0xE1,
};

static struct terminating_process_args implicit_exit_args = {
    "implicit-return",
    220000u,
    3u,
    0xB2,
};

static struct terminating_process_args spawner_exit_args = {
    "spawner",
    260000u,
    2u,
    0xA3,
};

static struct terminating_process_args spawned_child_exit_args = {
    "spawned-child",
    300000u,
    2u,
    0xC4,
};

static struct terminating_process_args last_exit_args = {
    "last-explicit-exit",
    360000u,
    5u,
    0x1D,
};

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

static void terminating_process_heartbeat(const char *name, long pid, uint64_t heartbeat) {
    user_puts("[term-test ");
    user_puts(name);
    user_puts("] heartbeat pid=");
    user_puthex((uint64_t)pid);
    user_puts(" n=");
    user_puthex(heartbeat);
    user_puts("\n");
}

static void run_terminating_heartbeats(struct terminating_process_args *cfg, long pid) {
    for (uint64_t i = 0; i < cfg->heartbeats; i++) {
        terminating_process_heartbeat(cfg->name, pid, i);
        process_test_delay(cfg->delay_loops);
    }
}

static void *explicit_exit_process_main(void *arg) {
    struct terminating_process_args *cfg = (struct terminating_process_args *)arg;
    long pid = getpid();

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] start explicit exit pid=");
    user_puthex((uint64_t)pid);
    user_puts("\n");

    run_terminating_heartbeats(cfg, pid);

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] calling exit code=");
    user_puthex((uint64_t)cfg->exit_code);
    user_puts("\n");

    exit(cfg->exit_code);

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] ERROR returned from exit syscall\n");

    while (1) {
        asm volatile ("wfe");
    }

    return NULL;
}

static void *implicit_exit_process_main(void *arg) {
    struct terminating_process_args *cfg = (struct terminating_process_args *)arg;
    long pid = getpid();

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] start implicit trampoline exit pid=");
    user_puthex((uint64_t)pid);
    user_puts("\n");

    run_terminating_heartbeats(cfg, pid);

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] returning code=");
    user_puthex((uint64_t)cfg->exit_code);
    user_puts(" for trampoline exit\n");

    return (void *)(uintptr_t)cfg->exit_code;
}

static void *spawning_exit_process_main(void *arg) {
    struct terminating_process_args *cfg = (struct terminating_process_args *)arg;
    long pid = getpid();

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] start spawn-and-exit pid=");
    user_puthex((uint64_t)pid);
    user_puts("\n");

    long child_pid = spawn(implicit_exit_process_main, &spawned_child_exit_args);
    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] spawned terminating child pid=");
    user_puthex((uint64_t)child_pid);
    user_puts("\n");

    run_terminating_heartbeats(cfg, pid);

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] explicit exit after spawn code=");
    user_puthex((uint64_t)cfg->exit_code);
    user_puts("\n");

    exit(cfg->exit_code);

    user_puts("[term-test ");
    user_puts(cfg->name);
    user_puts("] ERROR returned from exit syscall\n");

    while (1) {
        asm volatile ("wfe");
    }

    return NULL;
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

static void enqueue_terminating_process_or_report(const char *name, pid_t pid) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    uart_puts("[term-test] ");
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

static void run_terminating_process_self_tests(void) {
    uart_puts("[term-test] creating terminating process scheduler tests\n");
    uart_puts("[term-test] explicit process should call exit() itself\n");
    uart_puts("[term-test] implicit process should return and let trampoline call exit()\n");
    uart_puts("[term-test] spawner should create a terminating child, then exit\n");
    uart_puts("[term-test] when the final process exits, scheduler should switch to idle\n");

    pid_t explicit_pid = proc_create(explicit_exit_process_main, &explicit_exit_args, -1);
    enqueue_terminating_process_or_report("explicit-exit", explicit_pid);

    pid_t implicit_pid = proc_create(implicit_exit_process_main, &implicit_exit_args, -1);
    enqueue_terminating_process_or_report("implicit-return", implicit_pid);

    pid_t spawner_pid = proc_create(spawning_exit_process_main, &spawner_exit_args, -1);
    enqueue_terminating_process_or_report("spawner", spawner_pid);

    pid_t last_pid = proc_create(explicit_exit_process_main, &last_exit_args, -1);
    enqueue_terminating_process_or_report("last-explicit-exit", last_pid);

    uart_puts("[term-test] scheduler_start should now run finite processes only\n");
    uart_puts("[term-test] expected final state: repeated scheduler idle ticks with no term-test heartbeats\n");
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
#if PROCESS_TEST_MODE_TERMINATING
    run_terminating_process_self_tests();
#else
    run_process_self_tests();
#endif
#endif
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
