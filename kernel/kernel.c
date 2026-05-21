#include <stdint.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "memory/kernel_mem.h"
#include "memory/malloc.h"
#include "memory/page.h"
#include "memory/phys/pmm.h"
#include "memory/virt/vmm.h"
#include "uart/uart.h"
#include "syscall/syscall.h"
#include "syscall/u_syscall.h"

extern char __kernel_heap_start[];
extern char __kernel_heap_end[];
extern char exception_vectors[];

#define TRAP_TEST_FATAL_BRK 0
#define TRAP_TEST_FATAL_SVC 0
#define TRAP_TEST_UNDEFINED_INSTRUCTION 0
#define TRAP_TEST_DATA_ABORT 0
#define PROCESS_TESTS_ENABLED 0
#define VM_TESTS_ENABLED 1
#define VM_PROCESS_TESTS_ENABLED 1

#if defined(PLATFORM_QEMU)
#define VM_MEMORY_BASE UINT64_C(0x40000000)
#define VM_MEMORY_SIZE UINT64_C(0x08000000)
#else
#define VM_MEMORY_BASE UINT64_C(0x00000000)
#define VM_MEMORY_SIZE UINT64_C(0x40000000)
#endif
#define PROCESS_TESTS_ENABLED 0
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

static uint64_t vm_test_failures;

static void vm_test_expect(int condition, const char *name) {
    uart_puts("[vm-test] ");
    uart_puts(condition ? "PASS " : "FAIL ");
    uart_puts(name);
    uart_puts("\n");

    if (!condition) {
        vm_test_failures++;
    }
}

static int vm_test_streq(const char *a, const char *b) {
    uint64_t i = 0;
    while (a[i] != '\0' || b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void run_vm_self_tests(void) {
    vm_test_failures = 0;

    uart_puts("[vm-test] PMM total pages: ");
    uart_puthex(pmm_total_pages());
    uart_puts(" used pages: ");
    uart_puthex(pmm_used_pages());
    uart_putc('\n');

    void *page_a = alloc_page();
    void *page_b = alloc_page();

    vm_test_expect(page_a != NULL && page_b != NULL &&
                   page_aligned((uint64_t)(uintptr_t)page_a) &&
                   page_aligned((uint64_t)(uintptr_t)page_b) &&
                   page_a != page_b,
                   "pmm alloc alignment/distinct pages");

    if (page_a != NULL) {
        free_page(page_a);
    }
    if (page_b != NULL) {
        free_page(page_b);
    }

    void *page_c = alloc_page();
    int zeroed = page_c != NULL;
    if (page_c != NULL) {
        const unsigned char *bytes = (const unsigned char *)page_c;
        for (uint64_t i = 0; i < PAGE_SIZE; i++) {
            if (bytes[i] != 0) {
                zeroed = 0;
                break;
            }
        }
        free_page(page_c);
    }
    vm_test_expect(zeroed, "pmm returns zeroed pages");

    uint64_t kernel_main_pa = vm_virt_to_phys(vm_kernel_address_space(),
                                              (uint64_t)(uintptr_t)&run_vm_self_tests);
    vm_test_expect(kernel_main_pa == (uint64_t)(uintptr_t)&run_vm_self_tests,
                   "kernel identity translation");

    struct address_space *copy_as = vm_create_address_space();
    void *copy_page = alloc_page();
    uint64_t copy_va = VM_USER_HEAP_BASE + UINT64_C(0x00200000);
    const char copy_msg[] = "copy-user-ok";
    char copy_back[sizeof(copy_msg)];
    int copy_ok = copy_as != NULL && copy_page != NULL &&
                  vm_map_page(copy_as, copy_va, (uint64_t)(uintptr_t)copy_page,
                              VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_USER) == 0 &&
                  validate_user_range(copy_as, copy_va, sizeof(copy_msg), 0) == 0 &&
                  validate_user_range(copy_as, copy_va, sizeof(copy_msg), 1) == 0 &&
                  copy_to_user(copy_as, copy_va, copy_msg, sizeof(copy_msg)) == 0 &&
                  copy_from_user(copy_as, copy_back, copy_va, sizeof(copy_msg)) == 0 &&
                  vm_test_streq(copy_back, copy_msg);
    vm_test_expect(copy_ok, "copy_to_user/copy_from_user valid mapped buffer");
    vm_test_expect(copy_as != NULL &&
                   validate_user_range(copy_as, 0, 8, 0) != 0,
                   "validate_user_range rejects null page");
    if (copy_as != NULL) {
        vm_unmap_page(copy_as, copy_va);
        vm_test_expect(validate_user_range(copy_as, copy_va, 1, 0) != 0,
                       "validate_user_range rejects unmapped page");
    }

    struct address_space *as_a = vm_create_address_space();
    struct address_space *as_b = vm_create_address_space();
    void *phys_a = alloc_page();
    void *phys_b = alloc_page();
    uint64_t shared_va = VM_USER_HEAP_BASE + UINT64_C(0x00400000);
    const char msg_a[] = "process-a";
    const char msg_b[] = "process-b";
    char back_a[sizeof(msg_a)];
    char back_b[sizeof(msg_b)];

    int isolation_ok = as_a != NULL && as_b != NULL && phys_a != NULL && phys_b != NULL &&
                       vm_map_page(as_a, shared_va, (uint64_t)(uintptr_t)phys_a,
                                   VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_USER) == 0 &&
                       vm_map_page(as_b, shared_va, (uint64_t)(uintptr_t)phys_b,
                                   VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_USER) == 0 &&
                       vm_virt_to_phys(as_a, shared_va) != vm_virt_to_phys(as_b, shared_va) &&
                       copy_to_user(as_a, shared_va, msg_a, sizeof(msg_a)) == 0 &&
                       copy_to_user(as_b, shared_va, msg_b, sizeof(msg_b)) == 0 &&
                       copy_from_user(as_a, back_a, shared_va, sizeof(back_a)) == 0 &&
                       copy_from_user(as_b, back_b, shared_va, sizeof(back_b)) == 0 &&
                       vm_test_streq(back_a, msg_a) &&
                       vm_test_streq(back_b, msg_b);
    vm_test_expect(isolation_ok, "same user VA maps distinct physical pages per address space");

    vm_test_expect(copy_as != NULL &&
                   validate_user_range(copy_as,
                                       (uint64_t)(uintptr_t)&run_vm_self_tests,
                                       1, 0) != 0,
                   "validate_user_range rejects kernel text pointer");

    uart_puts("[vm-test] summary failures=");
    uart_puthex(vm_test_failures);
    uart_puts("\n");

    if (vm_test_failures == 0) {
        uart_puts("[vm-test] PASS kernel VM self-tests complete\n");
    } else {
        uart_puts("[vm-test] FAIL kernel VM self-tests had failures\n");
    }
}

#if PROCESS_TESTS_ENABLED
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
#endif

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
#endif

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

#if PROCESS_TESTS_ENABLED
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
#endif

static void vm_user_prefix(char tag) {
    putc('[');
    putc('U');
    putc(tag);
    putc(']');
    putc(' ');
}

static void vm_user_print_pid(char tag, long pid) {
    vm_user_prefix(tag);
    putc('p');
    putc('i');
    putc('d');
    putc('=');
    user_puthex((uint64_t)pid);
    putc('\n');
}

static void vm_user_print_value(char tag, char label, uint64_t value) {
    vm_user_prefix(tag);
    putc(label);
    putc('=');
    user_puthex(value);
    putc('\n');
}

static void *vm_user_normal_test(void *arg) {
    (void)arg;
    long pid = getpid();
    volatile uint64_t stack_probe = UINT64_C(0xfeedfacecafebeef);
    char stack_msg[8];

    stack_msg[0] = 'V';
    stack_msg[1] = 'M';
    stack_msg[2] = 'S';
    stack_msg[3] = 'T';
    stack_msg[4] = 'A';
    stack_msg[5] = 'C';
    stack_msg[6] = 'K';
    stack_msg[7] = '\n';

    vm_user_print_pid('N', pid);
    vm_user_print_value('N', 's', stack_probe);

    long valid_ret = write_console(stack_msg, sizeof(stack_msg));
    long null_ret = write_console(NULL, 1);

    vm_user_print_value('N', 'w', (uint64_t)valid_ret);
    if (null_ret == SYS_EFAULT) {
        vm_user_prefix('N');
        putc('n');
        putc('u');
        putc('l');
        putc('l');
        putc(':');
        putc('o');
        putc('k');
        putc('\n');
    } else {
        vm_user_print_value('N', 'e', (uint64_t)null_ret);
    }

    return (void *)(uintptr_t)0x51;
}

static void *vm_user_data_abort_test(void *arg) {
    (void)arg;
    vm_user_print_pid('D', getpid());
    vm_user_prefix('D');
    putc('f');
    putc('a');
    putc('u');
    putc('l');
    putc('t');
    putc('\n');
    *(volatile uint64_t *)UINT64_C(0x0000000000200000) = UINT64_C(0x12345678);
    vm_user_prefix('D');
    putc('B');
    putc('A');
    putc('D');
    putc('\n');
    return (void *)(uintptr_t)0xDA;
}

static void *vm_user_insn_abort_test(void *arg) {
    (void)arg;
    vm_user_print_pid('I', getpid());
    vm_user_prefix('I');
    putc('f');
    putc('a');
    putc('u');
    putc('l');
    putc('t');
    putc('\n');
    ((void (*)(void))UINT64_C(0x0000000000300000))();
    vm_user_prefix('I');
    putc('B');
    putc('A');
    putc('D');
    putc('\n');
    return (void *)(uintptr_t)0x1A;
}

static void enqueue_vm_process_or_report(const char *name, pid_t pid) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    uart_puts("[vm-user] ");
    uart_puts(name);
    uart_puts(" pid=");
    uart_puthex((uint64_t)pid);

    if (pcb == NULL) {
        uart_puts(" create FAILED\n");
        return;
    }

    uart_puts(" as=");
    uart_puthex((uint64_t)(uintptr_t)pcb->as);
    uart_puts(" stack=");
    uart_puthex(pcb->user_stack_base);
    uart_puts("..");
    uart_puthex(pcb->user_stack_top);
    uart_puts(" heap=");
    uart_puthex(pcb->user_heap_base);
    uart_puts("..");
    uart_puthex(pcb->user_heap_end);
    uart_puts("\n");

    if (pcb->as != NULL) {
        uart_puts("[vm-user] ");
        uart_puts(name);
        uart_puts(" heap PA=");
        uart_puthex(vm_virt_to_phys(pcb->as, pcb->user_heap_base));
        uart_puts(" stack PA=");
        uart_puthex(vm_virt_to_phys(pcb->as, pcb->user_stack_top - 1));
        uart_puts("\n");
    }

    add_task_to_scheduler(pcb);
}

static void run_vm_process_tests(void) {
    uart_puts("[vm-user] creating finite VM process tests\n");

    pid_t normal_pid = proc_create(vm_user_normal_test, NULL, -1);
    pid_t data_pid = proc_create(vm_user_data_abort_test, NULL, -1);
    pid_t insn_pid = proc_create(vm_user_insn_abort_test, NULL, -1);

    enqueue_vm_process_or_report("normal-syscall-stack", normal_pid);
    enqueue_vm_process_or_report("data-abort", data_pid);
    enqueue_vm_process_or_report("instruction-abort", insn_pid);

    pcb_t *normal = get_pcb_by_pid(normal_pid);
    pcb_t *data = get_pcb_by_pid(data_pid);
    if (normal != NULL && data != NULL && normal->as != NULL && data->as != NULL &&
        normal->user_heap_base == data->user_heap_base &&
        vm_virt_to_phys(normal->as, normal->user_heap_base) !=
        vm_virt_to_phys(data->as, data->user_heap_base)) {
        uart_puts("[vm-user] PASS process heap virtual addresses match and physical pages differ\n");
    } else {
        uart_puts("[vm-user] FAIL process heap isolation precheck\n");
    }

    uart_puts("[vm-user] scheduler_start should run one normal syscall test, then two controlled user faults, then idle\n");
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
    uart_puts("[irq-test] force timer PPI pending begin\n");
    irq_force_pending(30u);
    for (volatile uint64_t spin = 0; spin < 1000000u; spin++) {
        asm volatile("nop");
    }
    uart_puts("[irq-test] ticks after forced pending=");
    uart_puthex(timer_get_ticks());
    uart_puts("\n");

    kernel_mem_init(__kernel_heap_start, __kernel_heap_end);
    uart_puts("[boot] kernel heap ready\n");
    uart_puts("[boot] vm_init begin\n");
    vm_init(VM_MEMORY_BASE, VM_MEMORY_SIZE);
    uart_puts("[boot] vm_init done\n");
    uart_puts("[boot] vm_enable PTE=");
    uart_puthex(vm_debug_get_pte(vm_kernel_address_space(), (uint64_t)(uintptr_t)&vm_enable_kernel_mmu));
    uart_puts(" vectors PTE=");
    uart_puthex(vm_debug_get_pte(vm_kernel_address_space(), (uint64_t)(uintptr_t)exception_vectors));
    uart_puts("\n");
    uart_puts("[boot] vm_enable_kernel_mmu begin\n");
    vm_enable_kernel_mmu();
    uart_puts("[vm-test] MMU enabled with identity kernel mappings\n");

#if VM_TESTS_ENABLED
    run_vm_self_tests();
#endif

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
#if VM_PROCESS_TESTS_ENABLED
    run_vm_process_tests();
#endif
    scheduler_start();

    while (1) {
        timer_delay_ms(750);
        uart_puts("while loop heartbeat\n");
    }
}
