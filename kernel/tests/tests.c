#include "scheduler/process.h"
#include "syscall/u_syscall.h"
#include "uart/uart.h"

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

void scheduler_test(void) {
    uart_puts("[TEST] CREATING A\n");
    proc_create(process_a, NULL, 0);
    uart_puts("[TEST] A CREATED. CREATING B:\n");
    proc_create(process_b, NULL, 0);
    uart_puts("PROCS CREATED\n");
}