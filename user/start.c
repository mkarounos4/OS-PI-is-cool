#include "syscall.h"

void __attribute__((section(".text.user_entry"), noreturn))
user_thread_start(void *(*func)(void *), void *arg) {
    void *ret = func(arg);
    exit((int)(uintptr_t)ret);

    while (1) {
    }
}
