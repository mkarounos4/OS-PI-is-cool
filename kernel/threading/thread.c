#include "threading/thread.h"
#include "process.h"
#include "scheduler/scheduler.h"
#include "memory/kmalloc.h"
#include "memory/page_table/page_table.h"
#include "uart/uart.h"

static Vec threads;

static void __attribute__((noreturn)) thread_start_trampoline(void) {
    void *(*start_routine)(void *);
    void *arg;

    asm volatile("mov %0, x19" : "=r"(start_routine));
    asm volatile("mov %0, x20" : "=r"(arg));
    thread_exit(start_routine(arg));
}

void threads_init() {
    threads = vec_new(10, NULL);
}

tcb_t *thread_get_by_tid(tid_t tid) {
    if (tid < 0 || tid >= vec_len(&threads)) {
        return NULL;
    }
 
    if (((tcb_t*) vec_get(&threads, tid))->state == THREAD_UNUSED) {
        return NULL;
    }
 
    return vec_get(&threads, tid);
}

tid_t thread_create(pcb_t *parent_pcb, void *(*start_routine)(void*), void *arg) {
    // find next available thread slot
    tid_t tid = -1;
    for (int i = 0; i < vec_len(&threads); i++) {
        tcb_t *thd = (tcb_t*) vec_get(&threads, i);
        if (thd->state == THREAD_UNUSED) {
            tid = i;
            break;
        }
    }
    if (tid == -1) {
        tid = vec_len(&threads);
        tcb_t *tcb = kcalloc(1, sizeof(tcb_t));
        if (tcb == NULL) {
            return -1;
        }
        vec_push_back(&threads, tcb);
    }
 
    tcb_t *new_thread = (tcb_t*) vec_get(&threads, tid);
 
    // allocate per-thread kernel stack
    new_thread->kernel_stack = alloc_page();
    if (new_thread->kernel_stack == NULL) {
        return -1;
    }
 
    // initialize thread control block
    new_thread->tid = tid;
    new_thread->state = THREAD_READY;
    new_thread->pcb = parent_pcb;
    new_thread->return_value = NULL;
    new_thread->is_joinable = 1;
    new_thread->waiting_on_this = vec_new(2, NULL);

    sigemptyset(&new_thread->pending_signals);
    new_thread->mask = 0;
    new_thread->priority = 1;
    new_thread->blocked_until = 0;
    new_thread->waiting_for_flags = 0;
    new_thread->waiting_for_pid = -2;

    new_thread->entry_func = start_routine;
    new_thread->args = arg;
 
    // setup CPU context
    uint8_t *stack_top = new_thread->kernel_stack + THREAD_STACK_SIZE;
    new_thread->ctx.sp = (uint64_t)(uintptr_t)stack_top;
    new_thread->ctx.x19 = (uint64_t)(uintptr_t)start_routine;
    new_thread->ctx.x20 = (uint64_t)(uintptr_t)arg;
    new_thread->ctx.x21 = 0;
    new_thread->ctx.x22 = 0;
    new_thread->ctx.x23 = 0;
    new_thread->ctx.x24 = 0;
    new_thread->ctx.x25 = 0;
    new_thread->ctx.x26 = 0;
    new_thread->ctx.x27 = 0;
    new_thread->ctx.x28 = 0;
    new_thread->ctx.x29 = 0;
    new_thread->ctx.x30 = (uint64_t)(uintptr_t)thread_start_trampoline;
    new_thread->ctx.ttbr0_el1 = parent_pcb->ttbr0_el1;
    new_thread->ctx.ttbr0_el1_va = parent_pcb->ttbr0_el1_va;
 
    pcb_thread_change_state(parent_pcb, THREAD_UNUSED, THREAD_RUNNING);
    vec_push_back(&parent_pcb->tids, (ptr_t)(uintptr_t)new_thread->tid);

    return tid;
}

void thread_exit(void *retval) {
    tcb_t *thread = get_curr_thread();
    if (thread == NULL) {
        while (1) {
            asm volatile ("wfe");
        }
    }
 
    thread->return_value = retval;
 
    // wake up any waiting threads
    for (size_t i = 0; i < vec_len(&thread->waiting_on_this); i++) {
        tid_t waiting_tid = (tid_t)(uintptr_t)vec_get(&thread->waiting_on_this, i);
        tcb_t *waiting = thread_get_by_tid(waiting_tid);
        if (waiting != NULL && waiting->state == THREAD_STOPPED) {
            waiting->state = THREAD_READY;
            add_thread_to_scheduler(waiting);
        }
    }
    vec_clear(&thread->waiting_on_this);

    pcb_thread_change_state(thread->pcb, thread->state, THREAD_ZOMBIE);
    thread->state = THREAD_ZOMBIE;
 
    schedule_yield();
 
    // should not return
    while (1) {
        asm volatile ("wfe");
    }
}

int thread_join(tid_t tid, void **retval) {
    tcb_t *current = get_curr_thread();
    if (current == NULL || current->pcb == NULL || current->tid == tid) {
        return -1;
    }
    pcb_t *pcb = current->pcb;
 
    tcb_t *target = thread_get_by_tid(tid);
    if (target == NULL || !target->is_joinable) {
        return -1;
    }
 
    // thread already terminated -> get return value immediately
    if (target->state == THREAD_ZOMBIE) {
        if (retval != NULL) {
            *retval = target->return_value;
        }
        thread_cleanup(target);
        return 0;
    }
 
    // add current thread to target's waiting list
    vec_push_back(&target->waiting_on_this, (ptr_t)(uintptr_t)current->tid);
    current->state = THREAD_STOPPED;
    schedule_yield();
 
    // get the return value on wake
    if (retval != NULL) {
        *retval = target->return_value;
    }
    thread_cleanup(target);
 
    return 0;
}

int thread_detach(tid_t tid) {
    // look up thread from current process
    tcb_t *current = get_curr_thread();
    if (current == NULL) {
        return -1;
    }
 
    tcb_t *target = thread_get_by_tid(tid);
    if (target == NULL) {
        return -1;
    }
 
    target->is_joinable = 0;
    if (target->state == THREAD_ZOMBIE) {
        thread_cleanup(target);
    }
    return 0;
}

void thread_cleanup(tcb_t *target) {
    pcb_t *pcb = target->pcb;
    vec_destroy(&target->waiting_on_this);
    target->state = THREAD_UNUSED;
    
    for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
        tid_t tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
        if (tid == target->tid) {
            vec_erase(&pcb->tids, i);
            break;
        }
    }

    if (vec_len(&threads)-1 == target->tid) {
        vec_pop_back(&threads, NULL);
        kfree(target);
    }

    pcb_thread_change_state(pcb, THREAD_ZOMBIE, THREAD_UNUSED);
}

// Add stop/terminate/block/unblock/continue process (but reqs signal mask and handlers)
void stop_thread(tcb_t *tcb) {
    if (tcb->state == THREAD_STOPPED) {
        return;
    }

    pcb_thread_change_state(tcb->pcb, tcb->state, THREAD_STOPPED);
    tcb->state = THREAD_STOPPED;
    if (get_curr_thread() == tcb) {
        schedule_yield();
    }
}

void terminate_thread(tcb_t *tcb) {
    if (tcb->state == THREAD_ZOMBIE) {
        return;
    }

    pcb_thread_change_state(tcb->pcb, tcb->state, THREAD_ZOMBIE);
    tcb->state = THREAD_ZOMBIE;

    if (get_curr_thread() == tcb) {
        schedule_yield();
    }
}

void block_thread(tcb_t *tcb, int blocked_state) {
    if (blocked_state != THREAD_BLOCKED_INTERRUPTABLE && blocked_state != THREAD_BLOCKED_KILLABLE && blocked_state != THREAD_BLOCKED_UNINTERUPTABLE) {
        return;
    }

    pcb_thread_change_state(tcb->pcb, tcb->state, blocked_state);
    tcb->state = blocked_state;
    if (get_curr_thread() == tcb) {
        schedule_yield();
    }
}

void unblock_thread(tcb_t *tcb) {
    if (tcb == NULL) {
        return;
    }

    if (tcb->state != THREAD_BLOCKED_INTERRUPTABLE &&
        tcb->state != THREAD_BLOCKED_UNINTERUPTABLE &&
        tcb->state != THREAD_BLOCKED_KILLABLE) {
        return;
    }

    pcb_thread_change_state(tcb->pcb, tcb->state, THREAD_READY);
    tcb->state = THREAD_READY;
    tcb->blocked_until = 0;
    add_thread_to_scheduler(tcb);
}

void continue_thread(tcb_t *tcb) {
    if (tcb->state != THREAD_STOPPED) {
        return;
    }

    pcb_thread_change_state(tcb->pcb, tcb->state, THREAD_READY);
    tcb->state = THREAD_READY;
    add_thread_to_scheduler(tcb);
}


int send_unblock_event(tid_t tid, uint32_t event) {
    tcb_t *tcb = thread_get_by_tid(tid);
    if (tcb == NULL || (tcb->state != THREAD_BLOCKED_INTERRUPTABLE && tcb->state != THREAD_BLOCKED_UNINTERUPTABLE && tcb->state != THREAD_BLOCKED_KILLABLE)) {
        return 0;
    }

    if (tcb->blocked_until & event) {
        unblock_thread(tcb);
        return 1;
    }

    return 0;
}
