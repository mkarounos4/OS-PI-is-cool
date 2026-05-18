#include "uart.h"
#include <stdint.h>

// RP1 UART0 (PL011)
// RP1 datasheet: UART0 APB address = 0x40030000
// ARM physical  = RP1_PCIE_BASE (0x1f00000000) + 0x40030000
#define UART0_BASE      UINT64_C(0x1f40030000)

// PL011 register offsets
#define UART_DR         0x00
#define UART_FR         0x18
#define UART_IBRD       0x24
#define UART_FBRD       0x28
#define UART_LCRH       0x2C
#define UART_CR         0x30
#define UART_ICR        0x44

// UART_FR bits
#define FR_TXFF         (1u << 5)   // TX FIFO full
#define FR_BUSY         (1u << 3)   // UART busy

// UART_LCRH bits
#define LCRH_WLEN_8     (3u << 5)   // 8-bit words
#define LCRH_FEN        (1u << 4)   // enable FIFOs

// UART_CR bits
#define CR_UARTEN       (1u << 0)
#define CR_TXE          (1u << 8)
#define CR_RXE          (1u << 9)

// 115200 baud @ UARTCLK=48MHz: divisor=26.042 -> IBRD=26, FBRD=3
#define IBRD_115200     26u
#define FBRD_115200     3u

static inline void wr(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)addr = val;
}

static inline uint32_t rd(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}

void uart_init(void) {
    // GPIO 14/15 mux is handled by the firmware when enable_uart=1
    // is set in config.txt — no manual GPIO setup needed here.

    // Disable UART
    wr(UART0_BASE + UART_CR, 0);

    // Wait for any in-progress transmission to finish
    while (rd(UART0_BASE + UART_FR) & FR_BUSY);

    // Flush FIFOs
    wr(UART0_BASE + UART_LCRH, rd(UART0_BASE + UART_LCRH) & ~LCRH_FEN);

    // Clear all interrupts
    wr(UART0_BASE + UART_ICR, 0x7FF);

    // Baud rate — IBRD/FBRD must be written before LCRH (PL011 TRM §3.3.7)
    wr(UART0_BASE + UART_IBRD, IBRD_115200);
    wr(UART0_BASE + UART_FBRD, FBRD_115200);

    // 8-N-1, FIFOs on (this write also latches the baud divisors)
    wr(UART0_BASE + UART_LCRH, LCRH_WLEN_8 | LCRH_FEN);

    // Enable UART, TX, RX
    wr(UART0_BASE + UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
}

void uart_putc(char c) {
    while (rd(UART0_BASE + UART_FR) & FR_TXFF);
    wr(UART0_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
