#pragma once

#include <stddef.h>
#include <stdint.h>

#define UART_KERNEL_VA_BASE UINT64_C(0xffff000000000000)

static inline uint64_t rpi5_mmio_va(uint64_t address) {
    return UART_KERNEL_VA_BASE | (address & UINT64_C(0x0000ffffffffffff));
}

static inline void rpi5_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)rpi5_mmio_va(address) = value;
}
static inline uint32_t rpi5_mmio_read32(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)rpi5_mmio_va(address);
}

static inline void rpi5_mmio_barrier(void) {
    asm volatile("dsb sy" ::: "memory");
}

uint64_t get_uart_base(void);
void uart_init(void);
void uart_irq_init(void);
void uart_rx_interrupt_hook(void);
void uart_rx_interrupts_enable(void);
void uart_rx_interrupts_disable(void);
void uart_input_handler(void *data, size_t size);
void uart_puts(const char *s);
void uart_putc(const char c);
void uart_putint(int i);
void uart_puthex(uint64_t value);

void uart_raw_putc(const char c);
void uart_raw_puts(const char *s);

void printf(const char *fmt, ...);
