#include "uart.h"

#define MMIO_BASE   0x3F000000UL   /* change to 0xFE000000 for Pi 4 */

/* GPIO */
#define GPFSEL1     (*(volatile uint32_t *)(MMIO_BASE + 0x00200004))
#define GPPUD       (*(volatile uint32_t *)(MMIO_BASE + 0x00200094))
#define GPPUDCLK0   (*(volatile uint32_t *)(MMIO_BASE + 0x00200098))

/* PL011 UART0 */
#define UART0_BASE  (MMIO_BASE + 0x00201000)
#define UART_DR     (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR     (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD   (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD   (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCRH   (*(volatile uint32_t *)(UART0_BASE + 0x2C))
#define UART_CR     (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART_ICR    (*(volatile uint32_t *)(UART0_BASE + 0x44))

static void delay(int32_t count) {
    /* Coarse busy-loop; fine for early init */
    __asm__ volatile("1: subs %0,%0,#1; bne 1b" : "+r"(count));
}

void uart_init(void) {
    UART_CR = 0;            /* disable UART */
    UART_ICR = 0x7FF;       /* clear all interrupts */

    /* Set GPIO14 (TX) and GPIO15 (RX) to alt0 (PL011) */
    uint32_t sel = GPFSEL1;
    sel &= ~((7 << 12) | (7 << 15));   /* clear bits for GPIO14/15 */
    sel |=  (4 << 12) | (4 << 15);     /* alt0 = 0b100             */
    GPFSEL1 = sel;

    /* Disable pull-ups/downs */
    GPPUD = 0;
    delay(150);
    GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    GPPUDCLK0 = 0;

    /* 115200 baud: IBRD=1, FBRD=40 assumes UARTCLK=3 MHz from mailbox.
       If you configure a different clock via the VideoCore mailbox,
       adjust these values accordingly. */
    UART_IBRD = 1;
    UART_FBRD = 40;
    UART_LCRH = (3 << 5);   /* 8n1, FIFOs enabled */
    UART_CR   = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(char c) {
    while (UART_FR & (1 << 5));
    UART_DR = (uint32_t)c;
}

void uart_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s);
    }
}
