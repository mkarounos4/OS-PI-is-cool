#include <stdint.h>

#include "uart.h"

#if PLATFORM_RPI
#include "rpi5_addresses.h"

#define LED_GPIO 4u

static void gpio_set_function(unsigned pin, uint32_t function) {
    rpi5_mmio_write32(RPI5_GPIO_CTRL(pin), function);
}

static void gpio_init_output(unsigned pin) {
    gpio_set_function(pin, RPI5_GPIO_FUNC_SYS_RIO);
    rpi5_mmio_write32(RPI5_GPIO_PAD(pin),
                      RPI5_PAD_IE | RPI5_PAD_DRIVE_8MA | RPI5_PAD_SCHMITT);
    rpi5_mmio_write32(RPI5_RIO_CLR(RPI5_RIO_OUT), 1u << pin);
    rpi5_mmio_write32(RPI5_RIO_SET(RPI5_RIO_OE), 1u << pin);
    rpi5_mmio_barrier();
}

static void gpio_write(unsigned pin, int high) {
    if (high) {
        rpi5_mmio_write32(RPI5_RIO_SET(RPI5_RIO_OUT), 1u << pin);
    } else {
        rpi5_mmio_write32(RPI5_RIO_CLR(RPI5_RIO_OUT), 1u << pin);
    }
}
#endif

static void delay_cycles(uint64_t count) {
    while (count--) {
        asm volatile("nop" ::: "memory");
    }
}

void kernel_main(void) {
    uart_init();
    uart_puts("\nRaspberry Pi 5 bare-metal kernel entered\n");

#if PLATFORM_RPI
    uart_puts("RP1 UART0: ");
    uart_puthex(get_uart_base());

    gpio_init_output(LED_GPIO);
    gpio_write(LED_GPIO, 1);

#endif

    uart_puts("\nGPIO4: SYS_RIO output\n");
    uint64_t heartbeat = 0;

    while (1) {
#if PLATFORM_RPI
        gpio_write(LED_GPIO, 1);
#endif
        uart_puts("alive ");
        uart_puthex(heartbeat++);
        uart_puts("\n");
        delay_cycles(100000000ULL);

#if PLATFORM_RPI
        gpio_write(LED_GPIO, 0);
#endif
        delay_cycles(25000000ULL);
    }
}
