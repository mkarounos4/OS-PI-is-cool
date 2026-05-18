#pragma once

#include <stdint.h>

static inline void rpi5_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}
static inline uint32_t rpi5_mmio_read32(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}

static inline void rpi5_mmio_barrier(void) {
    asm volatile("dsb sy" ::: "memory");
}

uint64_t get_uart_base(void);
void uart_init(void);
void uart_puts(const char *s);
void uart_putc(const char c);
void uart_puthex(uint64_t value);

void uart_raw_putc(const char c);
void uart_raw_puts(const char *s);
