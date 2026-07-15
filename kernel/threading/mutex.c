#include "threading/thread.h"
#include <stdarg.h>
#include "scheduler/scheduler.h"
#include "data-structs/vec.h"
#include "string.h"

#define MAX_TRACKED_LOCKS 64

enum tracked_lock_type {
    TRACKED_LOCK_MUTEX,
    TRACKED_LOCK_SEM,
};

struct tracked_lock {
    uint8_t active;
    enum tracked_lock_type type;
    uint32_t id;
    void *lock;
};

static struct tracked_lock tracked_locks[MAX_TRACKED_LOCKS];
static uint32_t next_lock_id = 1;

static int append_lock(char *buf, size_t size, int len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t used = len < (int)size ? (size_t)len : size - 1;
    int ret = vsnprintf(buf + used, size - used, fmt, args);
    va_end(args);
    if (ret < 0) {
        return ret;
    }
    return len + ret;
}

static void make_lock_name(char name[16], const char *prefix, uint32_t id) {
    snprintf(name, 16, "%s%u", prefix, (unsigned int)id);
}

static void lock_registry_add(void *lock, enum tracked_lock_type type,
                              uint32_t id) {
    for (unsigned int i = 0; i < MAX_TRACKED_LOCKS; i++) {
        if (tracked_locks[i].active) {
            continue;
        }
        tracked_locks[i].active = 1;
        tracked_locks[i].type = type;
        tracked_locks[i].id = id;
        tracked_locks[i].lock = lock;
        return;
    }
}

static void lock_registry_remove(void *lock) {
    for (unsigned int i = 0; i < MAX_TRACKED_LOCKS; i++) {
        if (tracked_locks[i].active && tracked_locks[i].lock == lock) {
            tracked_locks[i].active = 0;
            tracked_locks[i].lock = NULL;
            return;
        }
    }
}

void threading_register_semaphore(semaphore_t *sem) {
    if (sem == NULL) {
        return;
    }
    sem->lock_id = next_lock_id++;
    make_lock_name(sem->lock_name, "sem", sem->lock_id);
    lock_registry_add(sem, TRACKED_LOCK_SEM, sem->lock_id);
}

void threading_unregister_semaphore(semaphore_t *sem) {
    lock_registry_remove(sem);
}

int threading_format_locks(char *buf, size_t size) {
    if (buf == NULL || size == 0) {
        return -1;
    }

    int len = snprintf(buf, size, "ID TYPE OWNER WAITERS NAME\n");
    for (unsigned int i = 0; i < MAX_TRACKED_LOCKS; i++) {
        if (!tracked_locks[i].active) {
            continue;
        }

        if (tracked_locks[i].type == TRACKED_LOCK_MUTEX) {
            mutex_t *mutex = (mutex_t *)tracked_locks[i].lock;
            const char *owner = "none";
            char owner_buf[32];
            if (mutex->owner_tid >= 0) {
                tcb_t *owner_thread = thread_get_by_tid(mutex->owner_tid);
                int pid = owner_thread != NULL && owner_thread->pcb != NULL ?
                          owner_thread->pcb->pid : -1;
                snprintf(owner_buf, sizeof(owner_buf), "pid=%d/tid=%d",
                         pid, mutex->owner_tid);
                owner = owner_buf;
            }
            len = append_lock(buf, size, len, "%u mutex %s %u %s\n",
                              (unsigned int)tracked_locks[i].id,
                              owner,
                              (unsigned int)vec_len(&mutex->waiting_threads),
                              mutex->lock_name);
        } else {
            semaphore_t *sem = (semaphore_t *)tracked_locks[i].lock;
            len = append_lock(buf, size, len, "%u sem none %u %s\n",
                              (unsigned int)tracked_locks[i].id,
                              (unsigned int)vec_len(&sem->waiting_threads),
                              sem->lock_name);
        }
    }
    return len;
}

int mutex_init(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    
    mutex->lock_id = next_lock_id++;
    make_lock_name(mutex->lock_name, "mutex", mutex->lock_id);
    mutex->owner_tid = -1;
    mutex->lock_count = 0;
    mutex->waiting_threads = vec_new(4, NULL);
    lock_registry_add(mutex, TRACKED_LOCK_MUTEX, mutex->lock_id);
    
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
    
    lock_registry_remove(mutex);
    vec_destroy(&mutex->waiting_threads);
    
    return 0;
}
