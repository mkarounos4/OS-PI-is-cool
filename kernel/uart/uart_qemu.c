#include "uart.h"

#include <stdint.h>

#define QEMU_RPI3_UART0_BASE UINT64_C(0x3f201000)

#define UART_DR 0x00u
#define UART_FR 0x18u

#define FR_TXFF (1u << 5)

void uart_init(void)
{
    // QEMU raspi3b firmware leaves PL011 usable for early serial output.
}

void uart_raw_putc(const char c)
{
    while ((rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_FR) & FR_TXFF) != 0) {
        asm volatile("yield" ::: "memory");
    }

    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

void uart_putc(const char c)
{
    if (c == '\n') {
        uart_raw_putc('\r');
    }

    uart_raw_putc(c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_raw_puts(const char *s)
{
    while (*s) {
        uart_raw_putc(*s++);
    }
}

void uart_puthex(uint64_t value)
{
    uart_puts("0x");

    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (value >> shift) & 0xfu;
        char c = nibble < 10 ? (char)('0' + nibble) : (char)('A' + (nibble - 10));

        if (started || c != '0' || shift == 0) {
            started = 1;
            uart_putc(c);
        }
    }
}
