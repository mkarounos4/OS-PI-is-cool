#include "rpi5_addresses.h"
#include "uart.h"
#include "devices/tty.h"

#include <stddef.h>

#include "irq/irq.h"

#define UART_DR     0x00
#define UART_FR     0x18
#define UART_IBRD   0x24
#define UART_FBRD   0x28
#define UART_LCRH   0x2c
#define UART_CR     0x30
#define UART_IFLS   0x34
#define UART_IMSC   0x38
#define UART_RIS    0x3c
#define UART_MIS    0x40
#define UART_ICR    0x44

#define FR_TXFF     (1u << 5)
#define FR_RXFE     (1u << 4)
#define FR_BUSY     (1u << 3)

#define LCRH_WLEN_8 (3u << 5)
#define LCRH_FEN    (1u << 4)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

#define UART_INT_RX     (1u << 4)
#define UART_INT_TX     (1u << 5)
#define UART_INT_RT     (1u << 6)
#define UART_INT_FE     (1u << 7)
#define UART_INT_PE     (1u << 8)
#define UART_INT_BE     (1u << 9)
#define UART_INT_OE     (1u << 10)
#define UART_INT_RX_ALL (UART_INT_RX | UART_INT_RT | UART_INT_FE | UART_INT_PE | UART_INT_BE | UART_INT_OE)

#define UART_IFLS_RX_1_8 0u
#define RP1_INT_UART0    25u
#define UART_RX_BUFFER_SIZE 256u

static unsigned char uart_rx_buffer[UART_RX_BUFFER_SIZE];

static void uart_rx_buffer_clear(void) {
    for (size_t i = 0; i < UART_RX_BUFFER_SIZE; i++) {
        uart_rx_buffer[i] = 0;
    }
}

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

#if defined(RP1_UART_IRQ_BRIDGE_READY)
static struct trap_frame *uart_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx) {
    (void)intid;
    (void)ctx;

    uint32_t status = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_MIS);
    if ((status & UART_INT_RX_ALL) == 0) {
        return frame;
    }

    uart_rx_interrupts_disable();
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_ICR, status & UART_INT_RX_ALL);
    uart_rx_interrupt_hook();
    return frame;
}
#endif

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
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, 0);

    // IBRD/FBRD must be written before LCRH; the LCRH write latches them.
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IBRD, UART_IBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_FBRD, UART_FBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IFLS, UART_IFLS_RX_1_8);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_LCRH, LCRH_WLEN_8 | LCRH_FEN);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
    rpi5_mmio_barrier();
}

void uart_irq_init(void) {
    /*
     * RP1_INT_UART0 is local to RP1. A future RP1 interrupt bridge must
     * translate it to the CPU interrupt controller before this can fire
     * on real Pi 5 hardware.
     */
#if defined(RP1_UART_IRQ_BRIDGE_READY)
    if (irq_register(RP1_INT_UART0, uart_irq_handler, NULL) == 0) {
        irq_enable_line(RP1_INT_UART0);
        uart_rx_interrupts_enable();
        uart_puts("[uart] RX IRQ armed\n");
    }
#else
    uart_puts("[uart] RX IRQ not armed; RP1 interrupt bridge required\n");
#endif
}

void uart_rx_interrupts_enable(void) {
    uint32_t mask = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, mask | UART_INT_RX_ALL);
    rpi5_mmio_barrier();
}

void uart_rx_interrupts_disable(void) {
    uint32_t mask = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, mask & ~UART_INT_RX_ALL);
    rpi5_mmio_barrier();
}

void uart_rx_interrupt_hook(void) {
    size_t size = 0;

    while ((rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_FR) & FR_RXFE) == 0) {
        uint32_t data = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_DR);
        if (size < UART_RX_BUFFER_SIZE) {
            uart_rx_buffer[size++] = (unsigned char)(data & 0xffu);
        }
    }

    if (size > 0) {
        tty_send_input(0, uart_rx_buffer, size);
    }

    uart_rx_buffer_clear();
    uart_rx_interrupts_enable();
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

void uart_putuint(unsigned int u)
{
    if (u >= 10)
        uart_putuint(u / 10);

    uart_putc('0' + (u % 10));
}

void uart_putint(int i)
{
    if (i < 0) {
        uart_putc('-');
        uart_putuint((unsigned int)(-i));
    } else {
        uart_putuint((unsigned int)i);
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
