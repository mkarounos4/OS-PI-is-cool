#pragma once

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
