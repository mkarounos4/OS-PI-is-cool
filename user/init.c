#include "syscall.h"

void *tests(void *args) {
    // TESTS HERE
    // scheduler_orphan_test();
    waitpid_signal_test();

}

void *init_process_entry(void*) {
    spawn(tests, NULL);

    while (1) {
        int ret = waitpid(-1, NULL, 0);
        if (ret == ECHILD) {
            block_until_event(BLOCK_UNTIL_NEW_CHILD);
        }
    }

    return NULL;
}

