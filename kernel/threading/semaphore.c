#include "threading/thread.h"
#include "scheduler/scheduler.h"
#include "data-structs/vec.h"

void threading_register_semaphore(semaphore_t *sem);
void threading_unregister_semaphore(semaphore_t *sem);

static void wake_thread(tid_t tid)
{
    tcb_t *current = get_curr_thread();
    if (current == NULL)
    {
        return;
    }

    tcb_t *thread = thread_get_by_tid(tid);
    if (thread == NULL) 
    {
        return;
    }

    if (thread->state == THREAD_STOPPED)
    {
        thread->state = THREAD_READY;
        add_thread_to_scheduler(thread);
    }
}

int sem_init(semaphore_t *sem, int initial_value)
{
    if (sem == NULL || initial_value < 0)
    {
        return -1;
    }

    sem->count = initial_value;
    sem->waiting_threads = vec_new(2, NULL);
    threading_register_semaphore(sem);

    return 0;
}

int sem_wait(semaphore_t *sem)
{
    if (sem == NULL) 
    {
        return -1;
    }

    sem->count--;

    if (sem->count >= 0) 
    {
        return 0;
    }

    tcb_t *current = get_curr_thread();
    if (current == NULL)
    {
        return -1;
    }

    vec_push_back(&sem->waiting_threads, (ptr_t)(uintptr_t)current->tid);

    current->state = THREAD_STOPPED;

    schedule_yield();

    return 0;
}

int sem_post(semaphore_t *sem)
{
    if (sem == NULL)
    {
        return -1;
    }

    sem->count++;

    if (sem->count <= 0)
    {
        if (vec_len(&sem->waiting_threads) > 0)
        {
            ptr_t tid = vec_get(&sem->waiting_threads, 0);
            vec_erase(&sem->waiting_threads, 0);

            wake_thread((tid_t)(uintptr_t)tid);
        }
    }

    return 0;
}

int sem_destroy(semaphore_t *sem) 
{
    if (sem == NULL) 
    {
        return -1;
    }

    if (vec_len(&sem->waiting_threads) != 0) 
    {
        return -1;
    }

    threading_unregister_semaphore(sem);
    vec_destroy(&sem->waiting_threads);

    sem->count = 0;

    return 0;
}
