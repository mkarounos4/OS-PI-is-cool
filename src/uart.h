#pragma once
#include <stdint.h>

/*
 * Raspberry Pi 5 / BCM2712 + RP1 only.
 *
 * RP1 peripheral registers are described by the RP1 datasheet at bus
 * addresses such as 0x40030000 for UART0 and 0x400d0000 for IO_BANK0.
 * The Pi 5 firmware reports the early RP1 UART at 0x1c00030000 when
 * enable_rp1_uart=1 is active, so use that handoff window for bare metal.
 */
#define RPI5_RP1_PERIPH_BASE        UINT64_C(0x1c00000000)

#define RPI5_RP1_UART0_BASE         (RPI5_RP1_PERIPH_BASE + UINT64_C(0x00030000))
#define RPI5_RP1_IO_BANK0_BASE      (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000d0000))
#define RPI5_RP1_RIO0_BASE          (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000e0000))
#define RPI5_RP1_PADS_BANK0_BASE    (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000f0000))

#define RPI5_GPIO_CTRL(pin) \
    (RPI5_RP1_IO_BANK0_BASE + ((uint64_t)(pin) * UINT64_C(8)) + UINT64_C(4))
#define RPI5_GPIO_PAD(pin) \
    (RPI5_RP1_PADS_BANK0_BASE + UINT64_C(4) + ((uint64_t)(pin) * UINT64_C(4)))

#define RPI5_RIO_OUT                (RPI5_RP1_RIO0_BASE + UINT64_C(0x00))
#define RPI5_RIO_OE                 (RPI5_RP1_RIO0_BASE + UINT64_C(0x04))
#define RPI5_RIO_SET(reg)           ((reg) + UINT64_C(0x2000))
#define RPI5_RIO_CLR(reg)           ((reg) + UINT64_C(0x3000))

#define RPI5_GPIO_FUNC_UART0        4u
#define RPI5_GPIO_FUNC_SYS_RIO      5u

#define RPI5_GPIO_CTRL_FUNCSEL_MASK (0x1fu << 0)
#define RPI5_GPIO_CTRL_OUTOVER_MASK (0x3u << 12)
#define RPI5_GPIO_CTRL_OEOVER_MASK  (0x3u << 14)
#define RPI5_GPIO_CTRL_INOVER_MASK  (0x3u << 16)

#define RPI5_PAD_OD                 (1u << 7)
#define RPI5_PAD_IE                 (1u << 6)
#define RPI5_PAD_DRIVE_2MA          (0u << 4)
#define RPI5_PAD_DRIVE_4MA          (1u << 4)
#define RPI5_PAD_DRIVE_8MA          (2u << 4)
#define RPI5_PAD_DRIVE_12MA         (3u << 4)
#define RPI5_PAD_PUE                (1u << 3)
#define RPI5_PAD_PDE                (1u << 2)
#define RPI5_PAD_SCHMITT            (1u << 1)
#define RPI5_PAD_SLEWFAST           (1u << 0)

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
void enable_uart(void);
void uart_puts(const char *s);
void uart_putc(const char c);
void uart_puthex(uint64_t value);

void uart_raw_putc(const char c);
void uart_raw_puts(const char *s);
