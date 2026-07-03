#pragma once

#include <stdint.h>

#define RPI5_PCIE2_BASE             UINT64_C(0x1000120000)
#define RPI5_MIP0_BASE              UINT64_C(0x1000130000)

#define RPI5_RP1_BAR0_BASE          UINT64_C(0x1f00000000)
#define RPI5_RP1_PERIPH_BASE        UINT64_C(0x1c00000000)

#define RPI5_RP1_UART0_BASE         (RPI5_RP1_PERIPH_BASE + UINT64_C(0x00030000))
#define RPI5_RP1_PWM1_BASE          (RPI5_RP1_PERIPH_BASE + UINT64_C(0x0009c000))
#define RPI5_RP1_PCIE_APBS_BASE     (RPI5_RP1_PERIPH_BASE + UINT64_C(0x00108000))
#define RPI5_RP1_SDIO0_BASE         (RPI5_RP1_PERIPH_BASE + UINT64_C(0x00180000))
#define RPI5_RP1_SDIO1_BASE         (RPI5_RP1_PERIPH_BASE + UINT64_C(0x00184000))
#define RPI5_RP1_SD_BASE            RPI5_RP1_SDIO1_BASE
#define RPI5_RP1_SDIO1_CLK_BASE     (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000b4004))
#define RPI5_RP1_IO_BANK0_BASE      (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000d0000))
#define RPI5_RP1_RIO0_BASE          (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000e0000))
#define RPI5_RP1_PADS_BANK0_BASE    (RPI5_RP1_PERIPH_BASE + UINT64_C(0x000f0000))

#define RPI5_BCM2712_PERIPH_BASE    UINT64_C(0x107c000000)
#define RPI5_BCM2712_SDIO1_BASE     (RPI5_BCM2712_PERIPH_BASE + UINT64_C(0x00fff000))
#define RPI5_BCM2712_SDIO1_CFG_BASE (RPI5_BCM2712_PERIPH_BASE + UINT64_C(0x00fff400))

#define RPI5_AON_GPIO_BASE          UINT64_C(0x107d517c00)
#define RPI5_AON_GPIO_DATA          (RPI5_AON_GPIO_BASE + UINT64_C(0x04))
#define RPI5_AON_GPIO_IODIR         (RPI5_AON_GPIO_BASE + UINT64_C(0x08))
#define RPI5_AON_SD_IOVDD_SEL       3u
#define RPI5_AON_SD_PWR_ON          4u
#define RPI5_AON_SD_CDET_N          5u

#define RPI5_GPIO_BANK_OFFSET(pin) \
    ((pin) < 28u ? UINT64_C(0x0000) : ((pin) < 34u ? UINT64_C(0x4000) : UINT64_C(0x8000)))
#define RPI5_GPIO_BANK_PIN(pin) \
    ((pin) < 28u ? (pin) : ((pin) < 34u ? ((pin) - 28u) : ((pin) - 34u)))

#define RPI5_GPIO_CTRL(pin) \
    (RPI5_RP1_IO_BANK0_BASE + RPI5_GPIO_BANK_OFFSET(pin) + \
     ((uint64_t)RPI5_GPIO_BANK_PIN(pin) * UINT64_C(8)) + UINT64_C(4))
#define RPI5_GPIO_PAD(pin) \
    (RPI5_RP1_PADS_BANK0_BASE + RPI5_GPIO_BANK_OFFSET(pin) + UINT64_C(4) + \
     ((uint64_t)RPI5_GPIO_BANK_PIN(pin) * UINT64_C(4)))

#define RPI5_RIO_OUT                (RPI5_RP1_RIO0_BASE + UINT64_C(0x00))
#define RPI5_RIO_OE                 (RPI5_RP1_RIO0_BASE + UINT64_C(0x04))
#define RPI5_RIO_SET(reg)           ((reg) + UINT64_C(0x2000))
#define RPI5_RIO_CLR(reg)           ((reg) + UINT64_C(0x3000))

#define RPI5_GPIO_FUNC_UART0        4u
#define RPI5_GPIO_FUNC_PWM1         0u
#define RPI5_GPIO_FUNC_SD1          0u
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
