#include "scheduler/scheduler.h"
#include "memory/kernel_mem.h"
#include "memory/malloc.h"
#include "traps/traps.h"
#include "syscall/u_syscall.h"
#include "uart/uart.h"

static pcb_t processes[MAX_PROCESS_COUNT];

// Aligns the given value down to the nearest multiple of the given alignment, which must be a power of 2.
static uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

static void process_trampoline(void *(*func)(void *), void *arg) {
    void *ret = func(arg);
    exit((int)(uintptr_t)ret);

    // Should not reach.
    while (1) {
        asm volatile ("wfe");
    }
}

long s_waitpid_impl(pid_t pid, int *status, int32_t flags) {
    (void) pid;
    (void) status;
    (void) flags;
    return 0;
}

// Returns pointer to next unused pcb, null
static pcb_t *get_next_unused_pcb() {
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (processes[i].state == PROC_UNUSED_STATE) {
            processes[i].pid = i;
            return &processes[i];
        }
    }
    return NULL;
}

pid_t proc_create(void *(*func)(void*), void *args, pid_t ppid) {
    // TODO add arguments

    pcb_t *new_proc = get_next_unused_pcb();
    if (new_proc == NULL) {
        uart_puts("ERROR: no unused processes left");
        return -1;
    }

    pcb_t *running_proc = get_curr_process();
    if (running_proc != NULL) {
        mem_fetch_heap_vals(&running_proc->heap_ctx);
    } else {
        mem_fetch_heap_vals(get_kernel_mem_ctx());
    }

    mem_load_heap(get_kernel_mem_ctx());

    // TODO: replace when VM added with better heap allocation
    new_proc->heap_ctx.heap_start = (void *)&new_proc->heap[0];
    new_proc->heap_ctx.heap_brk = new_proc->heap_ctx.heap_start;
    new_proc->heap_ctx.heap_end = (void *)&new_proc->heap[PROC_HEAP_SIZE];
    new_proc->heap_ctx.heap_ptr = NULL;
    new_proc->heap_ctx.seg_lists = NULL;

    mem_init(&new_proc->heap_ctx);
    mem_load_heap(get_kernel_mem_ctx());

    // Setup FD table here based on parent if applicable (after FS impl)
    new_proc->ppid = ppid;
    pcb_t *parent_proc = get_pcb_by_pid(ppid);
    if (parent_proc == NULL) {
        new_proc->pgid = new_proc->pid;
    } else {
        new_proc->pgid = parent_proc->pgid; 
        vec_push_back(&parent_proc->children, (ptr_t)(uintptr_t)new_proc->pid); 
    }

    // Setup children array
    new_proc->children = vec_new(5, NULL);

    new_proc->state = PROC_READY_STATE;
    new_proc->waiting_for_pid = -2;
    new_proc->wait_status_ptr = 0;
    new_proc->exit_code = 0;
    new_proc->name = NULL;

    // Get stacks for thread
    uintptr_t kernel_top = align_down((uintptr_t)&new_proc->kernel_stack[PROC_STACK_SIZE], 16);
    uintptr_t user_top = align_down((uintptr_t)&new_proc->user_stack[PROC_STACK_SIZE], 16);

    new_proc->user_stack_base = (uintptr_t) &new_proc->user_stack[0];
    new_proc->user_stack_top = (uintptr_t) user_top;
    new_proc->kernel_stack_base = (uintptr_t) &new_proc->kernel_stack[0];
    new_proc->kernel_stack_top = (uintptr_t) kernel_top;

    // Get trap_frame with thread context
    struct trap_frame *frame = (struct trap_frame *)(kernel_top - sizeof(struct trap_frame));

    // Initialize all thread registers to 0.
    for (unsigned i = 0; i < 31; i++) {
        frame->regs[i] = 0;
    }

    frame->regs[0] = (uint64_t)(uintptr_t)func;
    frame->regs[1] = (uint64_t)(uintptr_t)args;

    // Initialize all special registers.
    frame->sp = user_top;
    frame->elr = (uint64_t)(uintptr_t)process_trampoline;
    frame->spsr = 0; // Initialize to SP_EL0 for user exception level
    frame->esr = 0;
    frame->far = 0;
    frame->type = 0;
    frame->intid = 0;

    // Initialize rest of tcb
    new_proc->frame = frame;

    mem_fetch_heap_vals(get_kernel_mem_ctx());
    if (running_proc != NULL) {
        mem_load_heap(&running_proc->heap_ctx);
    } else {
        mem_load_heap(get_kernel_mem_ctx());
    }

    return new_proc->pid;
}

void proc_destroy(pcb_t *p) {
    (void) p;
}

pcb_t *get_pcb_by_pid(pid_t pid) {
    if (pid >= MAX_PROCESS_COUNT || pid < 0 || processes[pid].state == PROC_UNUSED_STATE) {
        return NULL;
    }

    return &processes[pid];
}

void processes_init() {
    for (unsigned int i = 0; i < MAX_PROCESS_COUNT; i++) {
        processes[i].state = PROC_UNUSED_STATE;
        processes[i].pid = i;
    }
}

#if 0
void init_process_entry(void*) {
    while (1) {
        waitpid(-1, NULL, 0);
    }
}
#endif

// Add stop/terminate/block/unblock/continue process (but reqs signal mask and handlers)
