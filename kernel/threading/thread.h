#include <stdint.h>
#include "traps/traps.h"
#include "data-structs/vec.h"

#ifndef TID_T_DEFINED
#define TID_T_DEFINED
typedef int32_t tid_t;
#endif

#define MAX_THREADS_PER_PROCESS 16
#define THREAD_STACK_SIZE 4096u

typedef struct pcb_st pcb_t;

// thread states
enum thread_state {
    THREAD_UNUSED,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_STOPPED,
    THREAD_ZOMBIE
};

// thread control block
typedef struct thread_st {
    tid_t tid;
    enum thread_state state;
    struct cpu_context ctx;
    struct pcb_st *pcb;
    uint8_t *kernel_stack;
    uint64_t *user_stack_va;
    void *return_value;
    uint8_t is_joinable;
    Vec waiting_on_this;
} thread_t;

tid_t thread_create(pcb_t *parent_pcb, void *(*start_routine)(void*), void *arg);
void thread_exit(void *retval) __attribute__((noreturn));
int thread_join(tid_t tid, void **retval);
int thread_detach(tid_t tid);
thread_t *get_curr_thread(void);
thread_t *thread_get_by_tid(pcb_t *pcb, tid_t tid);
int thread_is_valid(pcb_t *pcb, tid_t tid);
void threading_init(void);

/* synchronization structs */
// mutex
typedef struct {
    tid_t owner_tid;
    int lock_count;
    Vec waiting_threads;
} mutex_t;

// semaphore
typedef struct {
    int count;
    Vec waiting_threads;
} semaphore_t;

// condition variable
typedef struct {
    mutex_t *lock;
    Vec waiting_threads;
} condition_variable_t;

/* Mutex operations */
int mutex_init(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);
int mutex_destroy(mutex_t *mutex);

/* semaphore operations */
int sem_init(semaphore_t *sem, int initial_value);
int sem_wait(semaphore_t *sem);
int sem_post(semaphore_t *sem);
int sem_destroy(semaphore_t *sem);

/* condition variable operations */
int cond_init(condition_variable_t *cond, mutex_t *mutex);
int cond_wait(condition_variable_t *cond, mutex_t *mutex);
int cond_signal(condition_variable_t *cond);
int cond_broadcast(condition_variable_t *cond);
int cond_destroy(condition_variable_t *cond);

