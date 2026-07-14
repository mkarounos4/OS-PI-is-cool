#include "scheduler/scheduler.h"
#include "data-structs/hashmap.h"
#include "memory/kmalloc.h"
#include "memory/malloc.h"
#include "memory/page_table/page_table.h"
#include "traps/traps.h"
#include "uart/uart.h"
#include "signals/signals.h"
#include "user_image.h"

#define PA_MASK UINT64_C(0x0000ffffffffffff)

static pcb_t processes[MAX_PROCESS_COUNT];
static HashMap pgrps;

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

static void pgrp_destroy(hashmap_value_t value) {
    pgrp_t *pgrp = (pgrp_t *)value;
    if (pgrp == NULL) {
        return;
    }

    vec_destroy(&pgrp->pids);
    kfree(pgrp);
}

pgrp_t *get_pgrp_by_pgid(pid_t pgid) {
    hashmap_value_t value = NULL;
    if (!hashmap_get(&pgrps, HASHMAP_KEY_FROM_INT(pgid), &value)) {
        return NULL;
    }

    return (pgrp_t *)value;
}

static int add_to_pgrp(pid_t pid) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return -1;
    }

    pgrp_t *pgrp = get_pgrp_by_pgid(pcb->pgid);
    if (pgrp == NULL) {
        pgrp = kmalloc(sizeof(pgrp_t));
        if (pgrp == NULL) {
            return -1;
        }

        pgrp->pids = vec_new(2, NULL);
        pgrp->refcount = 0;
        pgrp->pgid = pcb->pgid;
        if (!hashmap_put(&pgrps, HASHMAP_KEY_FROM_INT(pgrp->pgid), pgrp)) {
            pgrp_destroy(pgrp);
            return -1;
        }
    }

    for (size_t i = 0; i < vec_len(&pgrp->pids); i++) {
        if ((pid_t)(uintptr_t)vec_get(&pgrp->pids, i) == pid) {
            return 0;
        }
    }

    vec_push_back(&pgrp->pids, (ptr_t)(uintptr_t)pid);
    pgrp->refcount++;
    return 0;
}

static void remove_from_pgrp(pid_t pid) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return;
    }

    pgrp_t *pgrp = get_pgrp_by_pgid(pcb->pgid);
    if (pgrp == NULL) {
        return;
    }

    for (size_t i = 0; i < vec_len(&pgrp->pids); i++) {
        if ((pid_t)(uintptr_t)vec_get(&pgrp->pids, i) == pid) {
            vec_erase(&pgrp->pids, i);
            pgrp->refcount--;
            break;
        }
    }

    if (pgrp->refcount <= 0) {
        hashmap_remove(&pgrps, HASHMAP_KEY_FROM_INT(pgrp->pgid), NULL);
    }
}

static int child_matches_wait(pid_t wait_pid, pcb_t *pcb) {
    if (wait_pid == -1) {
        return 1;
    }

    if (wait_pid < -1) {
        return pcb->pgid == -wait_pid;
    }

    return pcb->pid == wait_pid;
}

pcb_t *find_waitable_child(Vec *children, pid_t wait_pid, uint32_t flags) {
    for (size_t i = 0; i < vec_len(children); i++) {
        pcb_t *pcb = get_pcb_by_pid((pid_t)(uintptr_t)vec_get(children, i));
        if (pcb == NULL) {
            continue;
        }

        if (!child_matches_wait(wait_pid, pcb)) {
            continue;
        }

        if (can_wait_on_child(pcb, flags)) {
            return pcb;
        }
    }

    return NULL;
}

static int has_wait_child(Vec *children, pid_t wait_pid) {
    for (size_t i = 0; i < vec_len(children); i++) {
        pcb_t *pcb = get_pcb_by_pid((pid_t)(uintptr_t)vec_get(children, i));
        if (pcb != NULL && child_matches_wait(wait_pid, pcb)) {
            return 1;
        }
    }

    return 0;
}

long s_waitpid_impl(pid_t pid, int *status, int32_t flags) {
    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return SYS_ESRCH;
    }

    pcb_t *done_child;
    if (pid < 0) {
        if (!has_wait_child(&curr_pcb->children, pid)) {
            return ECHILD;
        }
        done_child = find_waitable_child(&curr_pcb->children, pid, flags);
        if (done_child == NULL) {
            if (flags & WNOHANG) {
                return 0;
            }

            curr_pcb->waiting_for_pid = pid;
            curr_pcb->waiting_for_flags = flags;
            while (done_child == NULL) {
                block_process(curr_pcb);
                done_child = find_waitable_child(&curr_pcb->children, pid, flags);
            }
            curr_pcb->waiting_for_pid = -2;
        }
    } else {
        done_child = get_pcb_by_pid(pid);
        if (done_child == NULL || done_child->ppid != curr_pcb->pid) {
            return SYS_ECHILD;
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

static const char *process_state_char(enum process_state state) {
    if (state == PROC_READY_STATE || state == PROC_RUNNING_STATE) {
        return "R";
    }
    if (state == PROC_BLOCKED_STATE) {
        return "B";
    }
    if (state == PROC_STOPPED_STATE) {
        return "S";
    }
    if (state == PROC_ZOMBIE_STATE) {
        return "Z";
    }
    return "?";
}

int print_processes(int fd) {
    int err = fprintf(fd, "PID PPID PRI STAT CMD\n");
    if (err < 0) {
        return err;
    }

    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        pcb_t *pcb = &processes[i];
        if (pcb->state == PROC_UNUSED_STATE) {
            continue;
        }

        err = fprintf(fd, "%d %d %d %s %s\n",
                      pcb->pid,
                      pcb->ppid,
                      pcb->priority,
                      process_state_char(pcb->state),
                      pcb->name);
        if (err < 0) {
            return err;
        }
    }

    return SUCCESS;
}

static void process_copy_name(pcb_t *pcb, const char *name) {
    size_t i = 0;
    if (name == NULL || name[0] == '\0') {
        name = "?";
    }

    while (i + 1 < sizeof(pcb->name) && name[i] != '\0') {
        pcb->name[i] = name[i];
        i++;
    }
    pcb->name[i] = '\0';
}

int set_process_name(const char *name) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    process_copy_name(pcb, name);
    return 0;
}

int set_process_name_for_pid(pid_t pid, const char *name) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    process_copy_name(pcb, name);
    return 0;
}

pid_t proc_create(void *(*func)(void*), void *args, pid_t ppid) {
    pcb_t *new_proc = get_next_unused_pcb();
    if (new_proc == NULL) {
        uart_puts("ERROR: no unused processes left");
        return (pid_t)SYS_EAGAIN;
    }

    // TODO: replace when VM added with better heap allocation
    // allocate default heap values here

    // Setup FD table here based on parent if applicable (after FS impl)
    new_proc->ppid = ppid;
    pcb_t *parent_proc = get_pcb_by_pid(ppid);
    if (parent_proc == NULL) {
        new_proc->pgid = new_proc->pid;
        new_proc->cwd = ROOT_INO;
    } else {
        new_proc->pgid = parent_proc->pgid; 
        vec_push_back(&parent_proc->children, (ptr_t)(uintptr_t)new_proc->pid); 
        new_proc->cwd = parent_proc->cwd;
    }

    // Setup children array
    new_proc->children = vec_new(5, NULL);
    new_proc->file_descriptors = vec_new(3, NULL);

    new_proc->state = PROC_READY_STATE;
    new_proc->waiting_for_pid = -2;
    new_proc->wait_stop_pending = 0;
    new_proc->wait_cont_pending = 0;
    new_proc->waiting_for_flags = 0;
    new_proc->exit_code = 0;
    process_copy_name(new_proc, parent_proc != NULL ? parent_proc->name : "?");
    new_proc->entry_func = func;
    new_proc->args = args;
    new_proc->priority = 1;
    new_proc->thread_count = 1;
    new_proc->next_tid = 1;
    new_proc->mask = 0;
    new_proc->pending_signals = 0;
    for (int i = 0; i < MAX_THREADS_PER_PROCESS; i++) {
        new_proc->threads[i].tid = i;
        new_proc->threads[i].state = THREAD_UNUSED;
        new_proc->threads[i].pcb = new_proc;
        new_proc->threads[i].kernel_stack = NULL;
        new_proc->threads[i].user_stack_va = NULL;
        new_proc->threads[i].return_value = NULL;
        new_proc->threads[i].is_joinable = 0;
    }
    for (int i = 0; i < 32; i++) {
        new_proc->sigactions[i].sa_handler = SIG_DFL;
        new_proc->sigactions[i].sa_mask = 0;
        new_proc->sigactions[i].sa_flags = 0;
    }

    uint64_t *user_l0 = initialize_user_page_table();
    if (user_l0 == NULL) {
        uart_puts("ERROR: failed to initialize user page table");
        return (pid_t)SYS_ENOMEM;
    }

    uint64_t kernel_stack_page_va = PROC_KERNEL_STACK_TOP - PAGE_SIZE;
    uint8_t *kernel_stack_page = pt_seed_kernel_page(user_l0,
                                                     kernel_stack_page_va);
    if (kernel_stack_page == NULL) {
        uart_puts("ERROR: failed to seed process kernel stack");
        return (pid_t)SYS_ENOMEM;
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
    new_proc->ctx.ttbr0_el1 = kernel_phys_addr((uint64_t)user_l0);
    new_proc->ctx.ttbr0_el1_va = (uint64_t)user_l0;

    thread_t *main_thread = &new_proc->threads[0];
    main_thread->tid = 0;
    main_thread->state = THREAD_READY;
    main_thread->ctx = new_proc->ctx;
    main_thread->pcb = new_proc;
    main_thread->kernel_stack = kernel_stack_page;
    main_thread->user_stack_va = (uint64_t *)(uintptr_t)USER_STACK_TOP;
    main_thread->return_value = NULL;
    main_thread->is_joinable = 0;
    main_thread->waiting_on_this = vec_new(2, NULL);

    if (add_to_pgrp(new_proc->pid) != 0) {
        uart_puts("ERROR: failed to add process to process group");
        return (pid_t)SYS_EAGAIN;
    }

    return new_proc->pid;
}

void proc_destroy(pcb_t *p) {
    uart_puts("Cleaning up ");
    uart_puthex(p->pid);
    uart_puts("\n");
    for (int i = 0; i < (int)vec_len(&p->file_descriptors); i++) {
        int k_fd = (int)(uintptr_t)vec_get(&p->file_descriptors, i);
        if (k_fd < 0) {
            continue;
        }

        struct oft_entry *entry;
        if (get_oft_entry_by_fd(k_fd, &entry) == SUCCESS) {
            k_close(entry);
        }
    }
    vec_destroy(&p->file_descriptors);
    remove_from_pgrp(p->pid);
    destroy_page_table((uint64_t *)(uintptr_t)p->ctx.ttbr0_el1_va);
    for (int i = 0; i < p->thread_count; i++) {
        if (p->threads[i].state != THREAD_UNUSED) {
            vec_destroy(&p->threads[i].waiting_on_this);
            p->threads[i].state = THREAD_UNUSED;
        }
    }
    p->thread_count = 0;
    p->next_tid = 0;

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
    pgrps = hashmap_new(16, pgrp_destroy);

    for (unsigned int i = 0; i < MAX_PROCESS_COUNT; i++) {
        processes[i].state = PROC_UNUSED_STATE;
        processes[i].pid = i;
    }

    pid_t pid = proc_create((void *(*)(void *))(uintptr_t)USER_INIT_PROCESS_ENTRY, NULL, 0);
    pcb_t *init_pcb = get_pcb_by_pid(pid);
    if (init_pcb != NULL) {
        char *argv[] = {"/bin/init", NULL};
        int err = k_exec_process(pid, "/bin/init", argv);
        if (err != SUCCESS) {
            uart_puts("ERROR: failed to exec /bin/init: ");
            uart_puthex((uint64_t)err);
            uart_puts("\n");
            process_copy_name(init_pcb, "init");
        }
    }
    add_task_to_scheduler(get_pcb_by_pid(pid));
}

void cpy_address_space(pcb_t *src, pcb_t *dst) {
    uint64_t *src_l0 = (uint64_t *)src->ctx.ttbr0_el1_va;
    uint64_t *dst_l0 = (uint64_t *)alloc_page();
    if (dst_l0 == NULL) return;
    if (copy_page_table_struct(src_l0, dst_l0) != SUCCESS) return;
    destroy_page_table((uint64_t *)(uintptr_t)dst->ctx.ttbr0_el1_va);
    dst->ctx.ttbr0_el1 = (uint64_t)(uintptr_t)kernel_phys_addr((uint64_t)(uintptr_t)dst_l0);
    dst->ctx.ttbr0_el1_va = (uint64_t)dst_l0;

    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
	    if ((src_l0[i] & DESC_VALID) == 0) continue;

	    uint64_t *src_l1 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l0[i] & PTE_ADDR_MASK);
        uint64_t *dst_l1 = (uint64_t *)alloc_page();
	    if (dst_l1 == NULL) return;
	    dst_l0[i] = table_desc(dst_l1);

	    for (size_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
	        if ((src_l1[j] & DESC_VALID) == 0) continue;

	        uint64_t *src_l2 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l1[j] & PTE_ADDR_MASK);
            uint64_t *dst_l2 = (uint64_t *)alloc_page();
            if (dst_l2 == NULL) return;
            dst_l1[j] = table_desc(dst_l2);

	        for (size_t k = 0; k < PAGE_TABLE_ENTRIES; k++) {
		        if ((src_l2[k] & DESC_VALID) == 0) continue;

		        uint64_t *src_l3 = (uint64_t *)(uintptr_t)kernel_direct_map_va(src_l2[k] & PTE_ADDR_MASK);
                uint64_t *dst_l3 = (uint64_t *)alloc_page();
                if (dst_l3 == NULL) return;
                dst_l2[k] = table_desc(dst_l3);

		        for (size_t l = 0; l < PAGE_TABLE_ENTRIES; l++) {
		            if ((src_l3[l] & DESC_VALID) == 0) continue;

		            uint64_t src_pa = src_l3[l] & PTE_ADDR_MASK;
                    uint64_t src_attrs = src_l3[l] & ~PTE_ADDR_MASK;

                    if (!pte_is_user(src_l3[l])) { // kernel/device mapping -> do not COW
                        uint64_t *dst_page = (uint64_t *)alloc_page();
                        if (dst_page == NULL) return;
                        uint64_t dst_pa = kernel_phys_addr((uint64_t)(uintptr_t)dst_page);
                        copy_phys_page(src_pa, dst_pa);

                        dst_l3[l] = (dst_pa & PTE_ADDR_MASK) | src_attrs | DESC_VALID | DESC_PAGE;
                    } else { // user mapping -> COW
                        if (pte_is_writable(src_l3[l])) { // page not readonly -> make readonly
                            pte_make_readonly_and_mark_cow(&src_l3[l]);
                            uint64_t child_pte = (src_pa & PTE_ADDR_MASK) | (src_l3[l] & ~PTE_ADDR_MASK);
                            child_pte &= ~(PTE_AP_EL0_RW | PTE_AP_EL1_RW); // clear write bits
                            child_pte |= PTE_AP_EL0_RO; // set EL0 read-only
                            dst_l3[l] = child_pte | DESC_VALID | DESC_PAGE;

                            inc_pte_refcount_pa(src_pa);
                        } else { // page already readonly -> share to dst
                            dst_l3[l] = src_l3[l];
                            inc_pte_refcount_pa(src_pa);
                        }
                    }

                }
	        }
	    }
    }
    tlb_invalidate_all_user();
}

pid_t fork(struct trap_frame *frame) {
    // create child process off of parent
    pcb_t *parent = get_curr_process();
    if (parent == NULL) {
        return (pid_t)SYS_ESRCH;
    }

    pid_t child_pid = proc_create(parent->entry_func, parent->args, parent->pid);
    if (child_pid < 0) return (pid_t)SYS_EAGAIN;
    pcb_t *child = get_pcb_by_pid(child_pid); 

    cpy_address_space(parent, child);

    uint64_t frame_va = (uint64_t)(uintptr_t)frame;
    uint64_t kernel_stack_page_va = PROC_KERNEL_STACK_TOP - PAGE_SIZE;
    uint64_t frame_offset = frame_va - kernel_stack_page_va;
    if (frame_offset >= PAGE_SIZE) {
        proc_destroy(child);
        return (pid_t)SYS_EINVAL;
    }

    void *child_kernel_stack_page =
        pt_get_mapped_page((uint64_t *)(uintptr_t)child->ctx.ttbr0_el1_va,
                           kernel_stack_page_va);
    if (child_kernel_stack_page == NULL) {
        proc_destroy(child);
        return (pid_t)SYS_EFAULT;
    }

    struct trap_frame *child_frame =
        (struct trap_frame *)(uintptr_t)((uint8_t *)child_kernel_stack_page +
                                         frame_offset);
    *child_frame = *frame;
    child_frame->regs[0] = 0;
    child->ctx.x19 = frame_va;
    child->ctx.sp = frame_va;
    child->threads[0].ctx = child->ctx;
    child->threads[0].state = THREAD_READY;

    for (size_t i = 0; i < vec_len(&parent->file_descriptors); i++) {
        void *fd = vec_get(&parent->file_descriptors, i);
        vec_push_back(&child->file_descriptors, fd);
        if ((int)(uintptr_t)fd >= 0) {
            k_file_add_reference((int)(uintptr_t) fd);
        }
    }
    
    // save_curr_context(&child->ctx);
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
        thread_t *thread = get_curr_thread();
        if (thread != NULL && thread->pcb == pcb) {
            thread->state = THREAD_STOPPED;
        }
        schedule_yield();
    }
}

void unblock_process(pcb_t *pcb) {
    if (pcb->state != PROC_BLOCKED_STATE) {
        return;
    }
    pcb->state = PROC_READY_STATE;
    pcb->blocked_until = 0;
    for (int i = 0; i < pcb->thread_count; i++) {
        if (pcb->threads[i].state == THREAD_STOPPED) {
            pcb->threads[i].state = THREAD_READY;
        }
    }
    add_task_to_scheduler(pcb);
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

int dup2(int oldfd, int newfd) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    if (oldfd < 0 || newfd < 0 ||
        (size_t)oldfd >= vec_len(&pcb->file_descriptors)) {
        return (int)SYS_EBADF;
    }

    if ((int)(uintptr_t)vec_get(&pcb->file_descriptors, oldfd) < 0) {
        return (int)SYS_EBADF;
    }

    close(newfd);
    vec_set(&pcb->file_descriptors, newfd, vec_get(&pcb->file_descriptors, oldfd));
    k_file_add_reference((int)(uintptr_t)vec_get(&pcb->file_descriptors, oldfd));

    return 0;
}

int setpgrp(pid_t pid, pid_t pgid) {
    pcb_t *pcb;
    if (pid == 0) {
        pcb = get_curr_process();
    } else {
        pcb = get_pcb_by_pid(pid);
    }

    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    if (pgid == 0) {
        pgid = pcb->pid;
    }

    if (pcb->pgid == pgid) {
        return 0;
    }

    pid_t old_pgid = pcb->pgid;
    remove_from_pgrp(pcb->pid);
    pcb->pgid = pgid;
    if (add_to_pgrp(pcb->pid) != 0) {
        pcb->pgid = old_pgid;
        add_to_pgrp(pcb->pid);
        return (int)SYS_EAGAIN;
    }
    return 0;
}

pid_t getpgid() {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return (pid_t)SYS_ESRCH;
    }

    return pcb->pgid;
}
