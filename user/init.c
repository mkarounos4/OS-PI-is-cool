#include "syscall.h"
#include "tests.h"
#include "shell.h"

void *tests(void *args) {
    (void)args;

    // TESTS HERE
    // scheduler_orphan_test();
    malloc_lazy_test();
    waitpid_signal_test();

    return NULL;
}

void *init_process_entry(void *args) {
    (void)args;

    int tty_num = 0;
    pid_t pid = spawn(shell_init, (void*)(uintptr_t)tty_num);
    setpgid(pid, pid);

    while (1) {
        int ret = waitpid(-1, NULL, 0);
        if (ret == ECHILD) {
            block_until_event(BLOCK_UNTIL_NEW_CHILD);
        }
    }

    return NULL;
}
