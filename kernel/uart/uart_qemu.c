// src/uart_qemu.c

#include "uart.h"
#include <stdint.h>

#define UART0 ((volatile uint32_t *)0x09000000)

void uart_init(void)
{
    // QEMU virt PL011 already initialized
}

void uart_raw_putc(const char c)
{
    *UART0 = (uint32_t)c;
}

void uart_putc(const char c)
{
    if (c == '\n') {
        *UART0 = '\r';
    }

    *UART0 = (uint32_t)c;
}

void uart_puts(const char *s)
{
    while (*s) {

        if (*s == '\n') {
            *UART0 = '\r';
        }

        *UART0 = (uint32_t)(*s);

        s++;
    }
}

void uart_raw_puts(const char *s)
{
    while (*s) {
        *UART0 = (uint32_t)(*s);
        s++;
    }
}

void uart_puthex(uint64_t value)
{
    uart_puts("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {

        uint8_t nibble =
            (value >> shift) & 0xF;

        char c;

        if (nibble < 10) {
            c = '0' + nibble;
        } else {
            c = 'A' + (nibble - 10);
        }

        uart_putc(c);
    }
}
