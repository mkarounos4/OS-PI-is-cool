#include "fan.h"

#include <stdint.h>

#include "uart/rpi5_addresses.h"
#include "uart/uart.h"

#define FAN_PWM_PIN 45u
#define FAN_PWM_RANGE 255u
#define FAN_PWM_DUTY FAN_PWM_RANGE

#define PWM_GLOBAL_CTRL 0x00u
#define PWM_CHAN3_CTRL 0x44u
#define PWM_CHAN3_RANGE 0x48u
#define PWM_CHAN3_PHASE 0x4cu
#define PWM_CHAN3_DUTY 0x50u

#define PWM_GLOBAL_SET_UPDATE (1u << 31)
#define PWM_GLOBAL_CHAN3_EN (1u << 3)
#define PWM_CHAN_CTRL_INVERT (1u << 3)
#define PWM_CHAN_CTRL_TRAILING_EDGE 1u

#if defined(PLATFORM_RPI5) || defined(PLATFORM_RPI)
static void fan_gpio_set_function(unsigned pin, uint32_t function) {
    uint32_t ctrl = rpi5_mmio_read32(RPI5_GPIO_CTRL(pin));
    ctrl &= ~RPI5_GPIO_CTRL_FUNCSEL_MASK;
    ctrl |= function;
    rpi5_mmio_write32(RPI5_GPIO_CTRL(pin), ctrl);
}
#endif

void fan_init(void) {
#if defined(PLATFORM_RPI5) || defined(PLATFORM_RPI)
    fan_gpio_set_function(FAN_PWM_PIN, RPI5_GPIO_FUNC_PWM1);
    rpi5_mmio_write32(RPI5_GPIO_PAD(FAN_PWM_PIN),
                      RPI5_PAD_DRIVE_8MA | RPI5_PAD_SCHMITT);

    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_GLOBAL_CTRL, 0);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_CHAN3_CTRL, 0);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_CHAN3_RANGE, FAN_PWM_RANGE);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_CHAN3_PHASE, 0);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_CHAN3_DUTY, FAN_PWM_DUTY);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_CHAN3_CTRL,
                      PWM_CHAN_CTRL_INVERT | PWM_CHAN_CTRL_TRAILING_EDGE);
    rpi5_mmio_write32(RPI5_RP1_PWM1_BASE + PWM_GLOBAL_CTRL,
                      PWM_GLOBAL_SET_UPDATE | PWM_GLOBAL_CHAN3_EN);
    rpi5_mmio_barrier();

    uart_puts("[fan] Pi 5 fan PWM enabled\n");
#endif
}
