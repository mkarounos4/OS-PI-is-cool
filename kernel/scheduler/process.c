#include "scheduler/scheduler.h"
#include "memory/page_table/page_table.h"
#include "traps/traps.h"
#include "uart/uart.h"
#include "signals/signals.h"
#include "user_image.h"

#define PA_MASK UINT64_C(0x0000ffffffffffff)

static pcb_t processes[MAX_PROCESS_COUNT];

static void __attribute__((noreturn)) process_first_run(void) {
    struct trap_frame *frame;
    asm volatile ("mov %0, x19" : "=r"(frame));
    trap_frame_restore(frame);
}

uint8_t can_wait_on_child(pcb_t *pcb, uint32_t flags) {
    if (pcb->state == PROC_ZOMBIE_STATE) {
        return 1;
    }
    if ((flags & WUNTRACED) && pcb->wait_stop_pending) {
        pcb->wait_stop_pending = 0;
        return 1;
    }
    if ((flags & WCONTINUED) && pcb->wait_cont_pending) {
        pcb->wait_cont_pending = 0;
        return 1;
    }
    return 0;
}

pcb_t *find_waitable_child(Vec *children, uint32_t flags) {
    for (size_t i = 0; i < vec_len(children); i++) {
        pcb_t *pcb = get_pcb_by_pid((pid_t)(uintptr_t)vec_get(children, i));
        if (pcb == NULL) {
            continue;
        }

        if (can_wait_on_child(pcb, flags)) {
            return pcb;
        }
    }

    return NULL;
}

long s_waitpid_impl(pid_t pid, int *status, int32_t flags) {
    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return -1;
    }

    pcb_t *done_child;
    if (pid == -1) {
        if (vec_len(&curr_pcb->children) == 0) {
            return ECHILD;
        }
        done_child = find_waitable_child(&curr_pcb->children, flags);
        if (done_child == NULL) {
            if (flags & WNOHANG) {
                return 0;
            }

            curr_pcb->waiting_for_pid = -1;
            curr_pcb->waiting_for_flags = flags;
            while (done_child == NULL) {
                block_process(curr_pcb);
                done_child = find_waitable_child(&curr_pcb->children, flags);
            }
            curr_pcb->waiting_for_pid = -2;
        }
    } else {
        done_child = get_pcb_by_pid(pid);
        if (done_child == NULL || done_child->ppid != curr_pcb->pid) {
            return -1;
        }

        if (!can_wait_on_child(done_child, flags)) {
            if (flags & WNOHANG) {
                return 0;
            }
            while (!can_wait_on_child(done_child, flags)) {
                curr_pcb->waiting_for_pid = pid;
                curr_pcb->waiting_for_flags = flags;
                block_process(curr_pcb);
            }
            curr_pcb->waiting_for_pid = -2;
        }
    }

    pid_t ret = done_child->pid;
    if (done_child->state == PROC_ZOMBIE_STATE) {
        if ((done_child->exit_code & 128) != 0) {
            if (status != NULL) *status = WAIT_SIGNALED;
        } else {
            if (status != NULL) *status = WAIT_EXITED;
        }
        proc_destroy(done_child);
    } else if (done_child->state == PROC_STOPPED_STATE) {
        if (status != NULL) *status = WAIT_STOPPED;
    } else if (status != NULL) {
        *status = WAIT_CONTINUED;
    }

    return ret;
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
    pcb_t *new_proc = get_next_unused_pcb();
    if (new_proc == NULL) {
        uart_puts("ERROR: no unused processes left");
        return -1;
    }

    // TODO: replace when VM added with better heap allocation
    // allocate default heap values here

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
    new_proc->wait_stop_pending = 0;
    new_proc->wait_cont_pending = 0;
    new_proc->waiting_for_flags = 0;
    new_proc->exit_code = 0;
    new_proc->name = NULL;
    new_proc->entry_func = func;
    new_proc->args = args;

    uint64_t *user_l0 = initialize_user_page_table();
    if (user_l0 == NULL) {
        uart_puts("ERROR: failed to initialize user page table");
        return -1;
    }

    uint64_t kernel_stack_page_va = PROC_KERNEL_STACK_TOP - PAGE_SIZE;
    uint8_t *kernel_stack_page = pt_seed_kernel_page(user_l0,
                                                     kernel_stack_page_va);
    if (kernel_stack_page == NULL) {
        uart_puts("ERROR: failed to seed process kernel stack");
        return -1;
    }

    uint64_t frame_va = PROC_KERNEL_STACK_TOP - sizeof(struct trap_frame);
    struct trap_frame *frame = (struct trap_frame *)(uintptr_t)
        (kernel_stack_page + (frame_va - kernel_stack_page_va));

    // Initialize all thread registers to 0.
    for (unsigned i = 0; i < 31; i++) {
        frame->regs[i] = 0;
    }

    frame->regs[0] = (uint64_t)(uintptr_t)func;
    frame->regs[1] = (uint64_t)(uintptr_t)args;

    // Initialize all special registers.
    frame->sp = USER_STACK_TOP;
    frame->elr = USER_THREAD_START;
    frame->spsr = 0; // Initialize to SP_EL0 for user exception level
    frame->esr = 0;
    frame->far = 0;
    frame->type = 0;
    frame->intid = 0;

    // Initialize rest of tcb
    new_proc->ctx.x19 = frame_va;
    new_proc->ctx.x20 = 0;
    new_proc->ctx.x21 = 0;
    new_proc->ctx.x22 = 0;
    new_proc->ctx.x23 = 0;
    new_proc->ctx.x24 = 0;
    new_proc->ctx.x25 = 0;
    new_proc->ctx.x26 = 0;
    new_proc->ctx.x27 = 0;
    new_proc->ctx.x28 = 0;
    new_proc->ctx.x29 = 0;
    new_proc->ctx.x30 = (uint64_t)(uintptr_t)process_first_run;
    new_proc->ctx.sp = frame_va;
    new_proc->ctx.ttbr0_el1 = kernel_phys_addr((uint64_t)(uintptr_t)user_l0);

    add_task_to_scheduler(new_proc);

    return new_proc->pid;
}

void proc_destroy(pcb_t *p) {
    uart_puts("Cleaning up ");
    uart_puthex(p->pid);
    uart_puts("\n");
    // Rn, stack and heap are tied to pcb, so automatically "free" when create new process
    // When adding VM, TODO add cleanup of stack/heap
    p->state = PROC_UNUSED_STATE;

    pcb_t *parent = get_pcb_by_pid(p->ppid);

    for (int i = 0; parent != NULL && i < (int)vec_len(&parent->children); i++) {
        if ((pid_t)(uintptr_t)vec_get(&parent->children, i) == p->pid) {
            vec_erase(&parent->children, i);
            break;
        }
    }
    
    // cleanup children
    pcb_t *init_proc = get_pcb_by_pid(0);
    int added_child = 0;
    while (!vec_is_empty(&p->children)) {
        ptr_t child_pid;
        vec_pop_back(&p->children, &child_pid);
        pcb_t *child_pcb = get_pcb_by_pid((pid_t)(uintptr_t)child_pid);
        if (child_pcb == NULL) {
            continue;
        }

        if (child_pcb->state == PROC_ZOMBIE_STATE) {
            proc_destroy(child_pcb);
        } else {
            vec_push_back(&init_proc->children, child_pid);
            child_pcb->ppid = init_proc->pid;
            added_child = 1;
        }
    }

    if (added_child) {
        send_unblock_event(init_proc->pid, BLOCK_UNTIL_NEW_CHILD);
    }
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

    proc_create((void *(*)(void *))(uintptr_t)USER_INIT_PROCESS_ENTRY, NULL, 0);
}

void cpy_address_space(pcb_t *src, pcb_t *dst) {
    uint64_t *src_l0 = (uint64_t *)(uintptr_t)src->ctx.ttbr0_el1;
    uint64_t *dst_l0 = (uint64_t *)alloc_page();
    if (dst_l0 == NULL) return;
    dst->ctx.ttbr0_el1 = (uint64_t)(uintptr_t)kernel_phys_addr((uint64_t)(uintptr_t)dst_l0);

    for (short i = 0; i < PAGE_TABLE_ENTRIES; i++) {
	if ((src_l0[i] & DESC_VALID) == 0) continue;

	uint64_t *src_l1 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l0[i] & PTE_ADDR_MASK);
        uint64_t *dst_l1 = (uint64_t *)alloc_page();
	if (dst_l1 == NULL) return;
	dst_l0[i] = table_desc(dst_l1);

	for (short j = 0; j < PAGE_TABLE_ENTRIES; j++) {
	    if ((src_l1[j] & DESC_VALID) == 0) continue;

	    uint64_t *src_l2 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l1[j] & PTE_ADDR_MASK);
            uint64_t *dst_l2 = (uint64_t *)alloc_page();
            if (dst_l2 == NULL) return;
            dst_l1[j] = table_desc(dst_l2);

	    for (short k = 0; k < PAGE_TABLE_ENTRIES; k++) {
		if ((src_l2[k] & DESC_VALID) == 0) continue;

		uint64_t *src_l3 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l2[k] & PTE_ADDR_MASK);
                uint64_t *dst_l3 = (uint64_t *)alloc_page();
                if (dst_l3 == NULL) return;
                dst_l2[k] = table_desc(dst_l3);

		for (short l = 0; l < PAGE_TABLE_ENTRIES; l++) {
		    if ((src_l3[l] & DESC_VALID) == 0) continue;

		    uint64_t src_pa = src_l3[l] & PTE_ADDR_MASK;
                    uint64_t *dst_page = (uint64_t *)alloc_page();
                    if (dst_page == NULL) return;

                    uint64_t dst_pa = kernel_phys_addr((uint64_t)(uintptr_t)dst_page);
                    copy_phys_page(src_pa, dst_pa);

                    uint64_t attrs = src_l3[l] & ~PTE_ADDR_MASK;
                    dst_l3[l] = (dst_pa & PTE_ADDR_MASK) | attrs;
		}
	    }
	}
    }
}

pid_t fork() {
    // create child process off of parent
    pcb_t *parent = get_curr_process();
    pid_t child_pid = proc_create(parent->entry_func, parent->args, parent->pid);
    if (child_pid == NULL) return -1;
    pcb_t *child = get_pcb_by_pid(child_pid); 
    
    // cpy parent trap frame over to child
    uint64_t parent_frame_va = parent->ctx.x19;
    struct trap_frame *parent_frame = (struct trap_frame *)(uintptr_t)parent_frame_va;
    uint64_t child_frame_va = child->ctx.x19;
    struct trap_frame *child_frame = (struct trap_frame *)(uintptr_t)child_frame_va;
    *child_frame = *parent_frame;

    // modify child return register
    child_frame->regs[0] = 0;

    cpy_address_space(child, parent);

    child->file_descriptors = parent->file_descriptors;

    // make child runnable
    add_task_to_scheduler(child);

    // return child pid to parent's call
    return child->pid;
}

// Add stop/terminate/block/unblock/continue process (but reqs signal mask and handlers)
void stop_process(pcb_t *pcb) {
    if (pcb->state == PROC_STOPPED_STATE) {
        return;
    }

    pcb->state = PROC_STOPPED_STATE;
    send_sigchld(pcb->pid);
    if (get_curr_process() == pcb) {
        schedule_yield();
    }
}

void terminate_process(pcb_t *pcb) {
    if (pcb->state == PROC_ZOMBIE_STATE) {
        return;
    }

    pcb->state = PROC_ZOMBIE_STATE;
    pcb->exit_code = 128;
    send_sigchld(pcb->pid);

    if (get_curr_process() == pcb) {
        schedule_yield();
    }
}

void block_process(pcb_t *pcb) {
    pcb->state = PROC_BLOCKED_STATE;
    if (get_curr_process() == pcb) {
        schedule_yield();
    }
}

void unblock_process(pcb_t *pcb) {
    if (pcb->state != PROC_BLOCKED_STATE) {
        return;
    }
    pcb->state = PROC_READY_STATE;
    pcb->blocked_until = 0;
    add_task_to_scheduler(pcb);

    // handle all queued signals
}

void continue_process(pcb_t *pcb) {
    if (pcb->state != PROC_STOPPED_STATE) {
        return;
    }

    pcb->state = PROC_READY_STATE;
    send_sigchld(pcb->pid);
    add_task_to_scheduler(pcb);
}

void send_unblock_event(pid_t pid, uint32_t event) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL || pcb->state != PROC_BLOCKED_STATE) {
        return;
    }

    if (pcb->blocked_until & event) {
        unblock_process(pcb);
    }
}
