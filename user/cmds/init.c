#include "lib/syscall.h"
#include "lib/signals.h"
#include "lib/tests.h"
#include "lib/stdio.h"

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

    pid_t pid = fork();

    setpgid(pid, pid);
    if (pid == 0) {
        char *argv[] = {"/bin/shell", NULL};
        exec("/bin/shell", argv);
        exit(-EIO);
    }

    while (1) {
        int ret = waitpid(-1, NULL, 0);
        if (ret == -ECHILD) {
            block_until_event(BLOCK_UNTIL_NEW_CHILD);
        }
    }

    return NULL;
}

static void ignore_all_signals(void) {
    struct sigaction action;
    sigfillset(&action.sa_mask);
    action.sa_handler = SIG_IGN;
    action.sa_flags = 0;

    for (int signum = 0; signum < 32; signum++) {
        sigaction(signum, &action, NULL);
    }
}

int main(int argc, char **argv) {
     (void)argc;
     ignore_all_signals();
     init_process_entry((void*) argv);

     return 0;
}
