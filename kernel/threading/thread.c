#include "threading/thread.h"
#include "scheduler/scheduler.h"
#include "memory/kmalloc.h"
#include "memory/page_table/page_table.h"
#include "uart/uart.h"

// global current thread ptr
static thread_t *curr_thread = NULL;

thread_t *get_curr_thread(void) {
    return curr_thread;
}

thread_t *thread_get_by_tid(pcb_t *pcb, tid_t tid) {
    if (pcb == NULL || tid < 0 || tid >= MAX_THREADS_PER_PROCESS) {
        return NULL;
    }
 
    if (pcb->threads[tid].state == THREAD_UNUSED) {
        return NULL;
    }
 
    return &pcb->threads[tid];
}

tid_t thread_create(pcb_t *parent_pcb, void *(*start_routine)(void*), void *arg) {
    if (parent_pcb == NULL || parent_pcb->thread_count >= MAX_THREADS_PER_PROCESS) {
        return -1;
    }
 
    // find next available thread slot
    tid_t tid = parent_pcb->next_tid;
    while (parent_pcb->threads[tid].state != THREAD_UNUSED) {
        tid = (tid + 1) % MAX_THREADS_PER_PROCESS;
        if (tid == parent_pcb->next_tid) {
            return -1;  // no available slots
        }
    }
 
    thread_t *new_thread = &parent_pcb->threads[tid];
 
    // allocate per-thread kernel stack
    new_thread->kernel_stack = alloc_page();
    if (new_thread->kernel_stack == NULL) {
        return -1;
    }
 
    // initialize thread control block
    new_thread->tid = tid;
    new_thread->state = THREAD_READY;
    new_thread->return_value = NULL;
    new_thread->is_joinable = 1;
    new_thread->waiting_on_this = vec_new(2, NULL);
 
    // setup CPU context
    uint8_t *stack_top = new_thread->kernel_stack + THREAD_STACK_SIZE;
    new_thread->ctx.sp = (uint64_t)(uintptr_t)stack_top;
    new_thread->ctx.x19 = (uint64_t)(uintptr_t)arg;     
    new_thread->ctx.x30 = (uint64_t)(uintptr_t)start_routine;  
 
    parent_pcb->thread_count++;
    parent_pcb->next_tid = (tid + 1) % MAX_THREADS_PER_PROCESS;
 
    // add to scheduler's ready queue
    add_thread_to_scheduler(new_thread, parent_pcb);

    return tid;
}

void thread_exit(void *retval) {
    thread_t *thread = get_curr_thread();
    if (thread == NULL) {
        return;
    }
 
    thread->return_value = retval;
    thread->state = THREAD_ZOMBIE;
 
    // wake up any waiting threads
    for (size_t i = 0; i < vec_len(&thread->waiting_on_this); i++) {
        ptr_t waiting_tid;
        vec_get(&thread->waiting_on_this, i);
        // mark ready to be woken
    }
 
    schedule_yield();
 
    // should not return
    while (1) {
        asm volatile ("wfe");
    }
}

int thread_join(tid_t tid, void **retval) {
    thread_t *current = get_curr_thread();
    pcb_t *pcb = current->pcb;
 
    thread_t *target = thread_get_by_tid(pcb, tid);
    if (target == NULL || !target->is_joinable) {
        return -1;
    }
 
    // thread already terminated -> get return value immediately
    if (target->state == THREAD_ZOMBIE) {
        if (retval != NULL) {
            *retval = target->return_value;
        }
        return 0;
    }
 
    // add current thread to target's waiting list
    vec_push_back(&target->waiting_on_this, (ptr_t*)current->tid);
    current->state = THREAD_STOPPED;
    schedule_yield();
 
    // get the return value on wake
    if (retval != NULL) {
        *retval = target->return_value;
    }
 
    return 0;
}

int thread_detach(tid_t tid) {
    // look up thread from current process
    thread_t *current = get_curr_thread();
    if (current == NULL) {
        return -1;
    }
 
    thread_t *target = thread_get_by_tid(current->pcb, tid);
    if (target == NULL) {
        return -1;
    }
 
    target->is_joinable = 0;
    return 0;
}
