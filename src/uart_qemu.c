// src/uart_qemu.c - semihosting based output
#include "uart.h"
#include <stdint.h>

void uart_init(void) { }

__attribute__((noinline))
void uart_putc(char c) {
    volatile char ch = c;   // force it onto the stack so &ch is valid
    __asm__ volatile(
        "mov x0, #3\n"
        "mov x1, %0\n"
        "hlt #0xF000\n"
        :
        : "r"((uint64_t)(uintptr_t)&ch)
        : "x0", "x1"
    );
}

__attribute__((noinline))
void uart_puts(const char *s) {
    for (; *s; s++) uart_putc(*s);
}
