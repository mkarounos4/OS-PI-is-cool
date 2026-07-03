#include "malloc.h"
#include "syscall.h"

static void *process_c(void *args);

void malloc_lazy_test(void) {
    char *buf = malloc(5000);
    if (buf == NULL) {
        putstr("[TEST] malloc failed\n");
        return;
    }

    for (int i = 0; i < 5000; i++) {
        buf[i] = (char)(i & 0x7f);
    }

    if (buf[4097] == (char)(4097 & 0x7f)) {
        putstr("[TEST] malloc lazy allocation ok\n");
    } else {
        putstr("[TEST] malloc lazy allocation mismatch\n");
    }

    free(buf);
}

static void *process_a(void *args) {
    (void) args;
    for (int i = 0; i < 4; i++) {
        putstr("PROC A WRITING\n");
        puthex(i);
        delay(3000);
    }
    putstr("PROC A DONE. SPAWNING!!!!\n");
    spawn(process_c, NULL);
    putstr("PROC A SPAWNED C.\n");
    return NULL;
}

static void *process_c(void *args) {
    (void) args;
    for (int i = 0; i < 5; i++) {
        putstr("PROC c WRITING\n");
        delay(300);
    }
    return NULL;
}

static void *process_b(void *args) {
    (void) args;
    for (int i = 0; i < 5; i++) {
        putstr("PROC B WRITING\n");
        puthex(i);
        delay(500);
    }
    putstr("B DONE\n");
    exit(0);
    return NULL;
}

void scheduler_orphan_test(void) {
    putstr("[TEST] CREATING A\n");
    spawn(process_a, NULL);
    putstr("[TEST] A CREATED. CREATING B:\n");
    spawn(process_b, NULL);
    putstr("PROCS CREATED\n");
}

static void *process_2(void *args) {
    (void) args;
    while (1) {

    }
    return NULL;
}

static void *process_3(void *args) {
    pid_t other_pid = (pid_t)(uintptr_t) args;
    putstr("Got child1. Stopping:\n");
    kill(other_pid, SIGSTOP);
    delay(2000);
    putstr("Now Continuing\n");
    kill(other_pid, SIGCONT);
    delay(2000);
    putstr("Now killing.\n");
    kill(other_pid, SIGKILL);
    exit(0);
    return NULL;
}

static void *process_1(void *args) {
    (void) args;
    pid_t pid1 = spawn(process_2, NULL);
    pid_t pid2 = spawn(process_3, (ptr_t) (uintptr_t) pid1);

    putstr("[P1] Spawned in child1: ");
    puthex(pid1);
    putstr("\n[P1] Spawned in child2: ");
    puthex(pid2);
    putc('\n');

    while (1) {
        pid_t cleaned = waitpid(-1, NULL, WUNTRACED);
        if (cleaned == ECHILD) {
            break;
        }
        putstr("[P1] GOT UPDATE FROM: ");
        puthex(cleaned);
        putstr("\n");
    }
    putstr("[P1] Finished cleaning\n");
    return NULL;
}

void waitpid_signal_test(void) {
    spawn(process_1, NULL);
}
