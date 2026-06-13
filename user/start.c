#include "malloc.h"
#include "syscall.h"

void __attribute__((section(".text.user_entry"), noreturn))
user_thread_start(void *(*func)(void *), void *arg) {
    mem_init((void *)(uintptr_t)USER_HEAP_START,
             (void *)(uintptr_t)(USER_HEAP_START + USER_HEAP_SIZE));

    void *ret = func(arg);
    exit((int)(uintptr_t)ret);

    while (1) {
    }
}
