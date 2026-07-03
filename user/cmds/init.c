#include "lib/syscall.h"
#include "lib/tests.h"

static int execvp(const char *path, void *arg) {
    (void)path;
    (void)arg;
    return -1;
}

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
    pid_t pid = fork();

    setpgid(pid, pid);
    if (pid == 0) {
        execvp("/bin/shell", (void*)(uintptr_t)tty_num);
        exit(-1);
    }

    while (1) {
        int ret = waitpid(-1, NULL, 0);
        if (ret == ECHILD) {
            block_until_event(BLOCK_UNTIL_NEW_CHILD);
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
     (void)argc;
     init_process_entry((void*) argv);

     return 0;
}
