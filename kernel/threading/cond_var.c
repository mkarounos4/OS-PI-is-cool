#include "threading/thread.h"
#include "scheduler/scheduler.h"
#include "data-structs/vec.h"

static void wake_thread(tid_t tid)
{
    thread_t *current = get_curr_thread();
    if (current == NULL)
    {
        return;
    }

    thread_t *thread = thread_get_by_tid(current->pcb, tid);
    if (thread == NULL)
    {
        return;
    }

    if (thread->state == THREAD_STOPPED)
    {
        thread->state = THREAD_READY;
        add_thread_to_scheduler(thread, current->pcb);
    }
}

int cond_init(condition_variable_t *cond, mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL)
    {
        return -1;
    }

    cond->lock = mutex;
    cond->waiting_threads = vec_new(2, NULL);

    return 0;
}

int cond_wait(condition_variable_t *cond, mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL)
    {
        return -1;
    }

    if (cond->lock != mutex)
    {
        return -1;
    }

    thread_t *current = get_curr_thread();
    if (current == NULL)
    {
        return -1;
    }

    vec_push_back(&cond->waiting_threads, (ptr_t)current->tid);

    mutex_unlock(mutex);

    current->state = THREAD_STOPPED;

    schedule_yield();

    // re-acquire mutex before waking
    mutex_lock(mutex);

    return 0;
}

int cond_signal(condition_variable_t *cond)
{
    if (cond == NULL)
    {
        return -1;
    }

    if (vec_len(&cond->waiting_threads) == 0)
    {
        return 0;
    }

    ptr_t tid = vec_get(&cond->waiting_threads, 0);
    vec_erase(&cond->waiting_threads, 0);

    wake_thread((tid_t)tid);

    return 0;
}

int cond_broadcast(condition_variable_t *cond)
{
    if (cond == NULL)
    {
        return -1;
    }

    while (vec_len(&cond->waiting_threads) > 0)
    {
        ptr_t tid = vec_get(&cond->waiting_threads, 0);
        vec_erase(&cond->waiting_threads, 0);

        wake_thread((tid_t)tid);
    }

    return 0;
}

int cond_destroy(condition_variable_t *cond)
{
    if (cond == NULL)
    {
	return -1;
    }

    if (vec_len(&cond->waiting_threads) != 0)
    {
        return -1;
    }

    vec_clear(&cond->waiting_threads);

    cond->lock = NULL;

    return 0;
}
