#include "scheduler/process.h"
#include "syscall/u_syscall.h"
#include "uart/uart.h"
#include "signals/signals.h"

static void *process_c(void *args);

static void *process_a(void *args) {
    (void) args;
    for (int i = 0; i < 4; i++) {
        write_console("PROC A WRITING\n", 16);
        uart_puthex(i);
        delay(3000);
    }
    write_console("PROC A DONE. SPAWNING!!!!\n", 36);
    spawn(process_c, NULL);
    write_console("PROC A SPAWNED C.\n", 18);
    return NULL;
}

static void *process_c(void *args) {
    (void) args;
    for (int i = 0; i < 5; i++) {
        write_console("PROC c WRITING\n", 16);
        delay(300);
    }
    return NULL;
}

static void *process_b(void *args) {
    (void) args;
    for (int i = 0; i < 5; i++) {
        write_console("PROC B WRITING\n", 16);
        uart_puthex(i);
        delay(500);
    }
    write_console("B DONE\n", 7);
    exit(0);
    return NULL;
}

void scheduler_orphan_test(void) {
    uart_puts("[TEST] CREATING A\n");
    proc_create(process_a, NULL, 0);
    uart_puts("[TEST] A CREATED. CREATING B:\n");
    proc_create(process_b, NULL, 0);
    uart_puts("PROCS CREATED\n");
}

static void *process_2(void *args) {
    (void) args;
    while (1) {

    }
    return NULL;
}

static void *process_3(void *args) {
    pid_t other_pid = (pid_t)(uintptr_t) args;
    write_console("Got child1. Stopping:\n", 22);
    kill(other_pid, SIGSTOP);
    delay(2000);
    write_console("Now Continuing\n", 15);
    kill(other_pid, SIGCONT);
    delay(2000);
    write_console("Now killing.\n", 13);
    kill(other_pid, SIGKILL);
    exit(0);
    return NULL;
}

static void *process_1(void *args) {
    (void) args;
    pid_t pid1 = spawn(process_2, NULL);
    pid_t pid2 = spawn(process_3, (ptr_t) (uintptr_t) pid1);

    write_console("[P1] Spawned in child1: ", 24);
    uart_puthex(pid1);
    write_console("\n[P1] Spawned in child2: ", 25);
    uart_puthex(pid2);
    uart_putc('\n');

    while (1) {
        pid_t cleaned = waitpid(-1, NULL, WUNTRACED);
        if (cleaned == ECHILD) {
            break;
        }
        write_console("[P1] GOT UPDATE FROM: ", 22);
        uart_puthex(cleaned);
        write_console("\n", 1);
    }
    write_console("[P1] Finished cleaning\n", 23);
    return NULL;
}

void waitpid_signal_test(void) {
    uart_puts("[TEST] Creating processes\n");
    proc_create(process_1, NULL, 0);
}