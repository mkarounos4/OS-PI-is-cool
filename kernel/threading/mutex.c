#include "threading/thread.h"
#include "scheduler/scheduler.h"
#include "data-structs/vec.h"

int mutex_init(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    mutex->owner_tid = -1;
    mutex->lock_count = 0;
    mutex->waiting_threads = vec_new(4, NULL);
    
    return 0;
}

int mutex_lock(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    tcb_t *current = get_curr_thread();
    if (current == NULL) {
        return -1;
    }
    
    // wait until lock acquired
    while (mutex->owner_tid != -1) {
        // add to waiting list
        vec_push_back(&mutex->waiting_threads, (ptr_t)(uintptr_t)current->tid);
        current->state = THREAD_STOPPED;
        schedule_yield();  // yield to scheduler
    }
    
    // lock owned
    mutex->owner_tid = current->tid;
    mutex->lock_count = 1;
    
    return 0;
}

int mutex_trylock(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    tcb_t *current = get_curr_thread();
    if (current == NULL) {
        return -1;
    }
    
    if (mutex->owner_tid != -1) {
        return -1;  // already locked
    }
    
    mutex->owner_tid = current->tid;
    mutex->lock_count = 1;
    
    return 0;
}

int mutex_unlock(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    tcb_t *current = get_curr_thread();
    if (current == NULL || mutex->owner_tid != current->tid) {
        return -1;  // not the owner
    }
    
    mutex->lock_count--;
    if (mutex->lock_count == 0) {
        mutex->owner_tid = -1;
        
        // wake up first waiting thread
        if (!vec_is_empty(&mutex->waiting_threads)) {
            ptr_t waiting_tid;
            vec_pop_back(&mutex->waiting_threads, &waiting_tid);
            
            pcb_t *pcb = current->pcb;
            tcb_t *waiting_thread = thread_get_by_tid((tid_t)(uintptr_t)waiting_tid);
            if (waiting_thread != NULL) {
                waiting_thread->state = THREAD_READY;
                add_thread_to_scheduler(waiting_thread);
            }
        }
    }
    
    return 0;
}

int mutex_destroy(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    if (mutex->owner_tid != -1 || !vec_is_empty(&mutex->waiting_threads)) {
        return -1;  // mutex still in use
    }
    
    vec_destroy(&mutex->waiting_threads);
    
    return 0;
}
