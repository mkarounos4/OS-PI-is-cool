#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "irq/irq.h"
#include "devices/tty.h"

#define QEMU_RPI3_UART0_BASE UINT64_C(0x3f201000)

#define UART_DR 0x00u
#define UART_FR 0x18u
#define UART_IFLS 0x34u
#define UART_IMSC 0x38u
#define UART_MIS 0x40u
#define UART_ICR 0x44u

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

#define UART_INT_RX     (1u << 4)
#define UART_INT_RT     (1u << 6)
#define UART_INT_FE     (1u << 7)
#define UART_INT_PE     (1u << 8)
#define UART_INT_BE     (1u << 9)
#define UART_INT_OE     (1u << 10)
#define UART_INT_RX_ALL (UART_INT_RX | UART_INT_RT | UART_INT_FE | UART_INT_PE | UART_INT_BE | UART_INT_OE)

#define UART_IFLS_RX_1_8 0u
#define QEMU_RPI3_UART0_INTID 57u
#define UART_RX_BUFFER_SIZE 256u

static unsigned char uart_rx_buffer[UART_RX_BUFFER_SIZE];

static void uart_rx_buffer_clear(void)
{
    for (size_t i = 0; i < UART_RX_BUFFER_SIZE; i++) {
        uart_rx_buffer[i] = 0;
    }
}

static struct trap_frame *uart_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx)
{
    (void)intid;
    (void)ctx;

    uint32_t status = rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_MIS);
    if ((status & UART_INT_RX_ALL) == 0) {
        return frame;
    }

    uart_rx_interrupts_disable();
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_ICR, status & UART_INT_RX_ALL);
    uart_rx_interrupt_hook();
    return frame;
}

void uart_init(void)
{
    // QEMU raspi3b firmware leaves PL011 usable for early serial output.
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_IMSC, 0);
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_ICR, 0x7ffu);
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_IFLS, UART_IFLS_RX_1_8);
}

void uart_irq_init(void)
{
    if (irq_register(QEMU_RPI3_UART0_INTID, uart_irq_handler, NULL) == 0) {
        irq_enable_line(QEMU_RPI3_UART0_INTID);
        uart_rx_interrupts_enable();
    }
}

void uart_rx_interrupts_enable(void)
{
    uint32_t mask = rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_IMSC, mask | UART_INT_RX_ALL);
}

void uart_rx_interrupts_disable(void)
{
    uint32_t mask = rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(QEMU_RPI3_UART0_BASE + UART_IMSC, mask & ~UART_INT_RX_ALL);
}

void uart_rx_interrupt_hook(void)
{
    size_t size = 0;

    while ((rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_FR) & FR_RXFE) == 0) {
        uint32_t data = rpi5_mmio_read32(QEMU_RPI3_UART0_BASE + UART_DR);
        if (size < UART_RX_BUFFER_SIZE) {
            char next = (unsigned char)(data & 0xffu);
            if (next == 0x0D) {
                next = '\n';
            }
            uart_rx_buffer[size++] = next;
        }
    }

    if (size > 0) {
        tty_send_input(0, (const char *)uart_rx_buffer, size);
    }

    uart_rx_buffer_clear();
    uart_rx_interrupts_enable();
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
