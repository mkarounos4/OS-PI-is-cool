#include "syscall.h"
#include "tests.h"

void *tests(void *args) {
    (void)args;

    // TESTS HERE
    // scheduler_orphan_test();
    waitpid_signal_test();

    return NULL;
}

void *init_process_entry(void *args) {
    (void)args;

    spawn(tests, NULL);

    while (1) {
        int ret = waitpid(-1, NULL, 0);
        if (ret == ECHILD) {
            block_until_event(BLOCK_UNTIL_NEW_CHILD);
        }
    }

    return NULL;
}
