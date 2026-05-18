#include "uart.h"

#define UART_DR     0x00
#define UART_FR     0x18
#define UART_IBRD   0x24
#define UART_FBRD   0x28
#define UART_LCRH   0x2c
#define UART_CR     0x30
#define UART_ICR    0x44

#define FR_TXFF     (1u << 5)
#define FR_BUSY     (1u << 3)

#define LCRH_WLEN_8 (3u << 5)
#define LCRH_FEN    (1u << 4)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

/*
 * Circle uses 50 MHz for RP1 PL011 UARTs on Pi 5 after firmware handoff.
 * Override this from CFLAGS if you deliberately reprogram RP1_CLK_UART.
 */
#ifndef RP1_UARTCLK_HZ
#define RP1_UARTCLK_HZ 50000000u
#endif

#define UART_BAUD        115200u
#define UART_DIV_X64     (((RP1_UARTCLK_HZ * 4u) + (UART_BAUD / 2u)) / UART_BAUD)
#define UART_IBRD_VALUE  (UART_DIV_X64 / 64u)
#define UART_FBRD_VALUE  (UART_DIV_X64 % 64u)
#define UART_POLL_LIMIT  1000000u

static void gpio_set_function(unsigned pin, uint32_t function) {
    rpi5_mmio_write32(RPI5_GPIO_CTRL(pin), function);
}

static void gpio_set_pad(unsigned pin, uint32_t pad_value) {
    rpi5_mmio_write32(RPI5_GPIO_PAD(pin), pad_value);
}

uint64_t get_uart_base(void) {
    return RPI5_RP1_UART0_BASE;
}

void uart_init(void) {
    // GPIO14/15 are pins 8/10 on the Pi 5 40-pin header.
    gpio_set_function(14, RPI5_GPIO_FUNC_UART0);
    gpio_set_function(15, RPI5_GPIO_FUNC_UART0);
    gpio_set_pad(14, RPI5_PAD_IE | RPI5_PAD_DRIVE_8MA | RPI5_PAD_SCHMITT);
    gpio_set_pad(15, RPI5_PAD_IE | RPI5_PAD_DRIVE_8MA | RPI5_PAD_PUE | RPI5_PAD_SCHMITT);

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_CR, 0);

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_LCRH, 0);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_ICR, 0x7ffu);

    // IBRD/FBRD must be written before LCRH; the LCRH write latches them.
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IBRD, UART_IBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_FBRD, UART_FBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_LCRH, LCRH_WLEN_8 | LCRH_FEN);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
    rpi5_mmio_barrier();
}

void enable_uart(void) {
    uart_init();
}

void uart_raw_putc(const char c) {
    uint32_t timeout = UART_POLL_LIMIT;

    while ((rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_FR) & FR_TXFF) && timeout) {
        timeout--;
    }

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

void uart_putc(const char c) {
    uart_raw_putc(c);
}

void uart_puts(const char *s) {
    uart_raw_puts(s);
}

void uart_raw_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_raw_putc('\r');
        }
        uart_raw_putc(*s++);
    }
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
