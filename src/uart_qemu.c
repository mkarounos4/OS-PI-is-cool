// src/uart_qemu.c - semihosting based output
#include "uart.h"
#include <stdint.h>

void uart_init(void) { }

__attribute__((noinline))
void uart_raw_putc(const char c) {
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
void uart_putc(const char c) {
    uart_raw_putc(c);
}

__attribute__((noinline))
void uart_puts(const char *s) {
    uart_raw_puts(s);
}

__attribute__((noinline))
void uart_raw_puts(const char *s) {
    for (; *s; s++) uart_putc(*s);
}

void uart_puthex(uint64_t value) {
    uart_raw_putc('0');
    uart_raw_putc('x');

    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xf;
        char c = nibble < 10 ? (char)('0' + nibble) : (char)('A' + (nibble - 10));

        if (started || c != '0' || i == 0) {
            started = 1;
            uart_raw_putc(c);
        }
    }
}
