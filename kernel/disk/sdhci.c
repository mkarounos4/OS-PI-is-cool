#include "sdhci.h"

#include <stddef.h>
#include <stdint.h>

#include "timer/timer.h"
#include "uart/rpi5_addresses.h"
#include "uart/uart.h"

#define SDHCI_BLOCK_SIZE_REG       0x04u
#define SDHCI_BLOCK_COUNT          0x06u
#define SDHCI_ARGUMENT             0x08u
#define SDHCI_TRANSFER_MODE        0x0cu
#define SDHCI_COMMAND              0x0eu
#define SDHCI_RESPONSE0            0x10u
#define SDHCI_RESPONSE1            0x14u
#define SDHCI_RESPONSE2            0x18u
#define SDHCI_RESPONSE3            0x1cu
#define SDHCI_BUFFER_DATA          0x20u
#define SDHCI_PRESENT_STATE        0x24u
#define SDHCI_HOST_CONTROL1        0x28u
#define SDHCI_POWER_CONTROL        0x29u
#define SDHCI_CLOCK_CONTROL        0x2cu
#define SDHCI_TIMEOUT_CONTROL      0x2eu
#define SDHCI_SOFTWARE_RESET       0x2fu
#define SDHCI_INT_STATUS           0x30u
#define SDHCI_INT_ENABLE           0x34u
#define SDHCI_SIGNAL_ENABLE        0x38u
#define SDHCI_HOST_CONTROL2        0x3cu
#define SDHCI_CAPABILITIES         0x40u

#define SDHCI_STATE_CMD_INHIBIT    (1u << 0)
#define SDHCI_STATE_DAT_INHIBIT    (1u << 1)
#define SDHCI_STATE_WRITE_READY    (1u << 10)
#define SDHCI_STATE_READ_READY     (1u << 11)

#define SDHCI_INT_CMD_COMPLETE     (1u << 0)
#define SDHCI_INT_TRANSFER_DONE    (1u << 1)
#define SDHCI_INT_BUF_WRITE_READY  (1u << 4)
#define SDHCI_INT_BUF_READ_READY   (1u << 5)
#define SDHCI_INT_ERROR            (1u << 15)
#define SDHCI_INT_CMD_TIMEOUT      (1u << 16)
#define SDHCI_INT_DATA_TIMEOUT     (1u << 20)
#define SDHCI_INT_DATA_CRC         (1u << 21)
#define SDHCI_INT_DATA_END_BIT     (1u << 22)
#define SDHCI_INT_ALL              0xffffffffu

#define SDHCI_RESET_ALL            (1u << 0)
#define SDHCI_RESET_CMD            (1u << 1)
#define SDHCI_RESET_DATA           (1u << 2)
#define SDHCI_CLOCK_INT_ENABLE     (1u << 0)
#define SDHCI_CLOCK_INT_STABLE     (1u << 1)
#define SDHCI_CLOCK_SD_ENABLE      (1u << 2)
#define SDHCI_TIMEOUT_MAX          0x0eu

#define SDHCI_POWER_3V3            0x0fu
#define SDHCI_HOST_4BIT            (1u << 1)

#define SD_BLOCK_SIZE_BYTES        512u
#define SD_WORDS_PER_BLOCK         (SD_BLOCK_SIZE_BYTES / sizeof(uint32_t))

#define SD_CMD_INDEX(cmd)          ((uint16_t)((cmd) << 8))
#define SD_CMD_RESP_NONE           0u
#define SD_CMD_RESP_136            1u
#define SD_CMD_RESP_48             2u
#define SD_CMD_RESP_48_BUSY        3u
#define SD_CMD_RESP_MASK           3u
#define SD_CMD_CRC_CHECK           (1u << 3)
#define SD_CMD_INDEX_CHECK         (1u << 4)
#define SD_CMD_DATA_PRESENT        (1u << 5)

#define SD_TM_BLOCK_COUNT_ENABLE   (1u << 1)
#define SD_TM_MULTI_BLOCK          (1u << 5)
#define SD_TM_READ                 (1u << 4)

#define SD_OCR_BUSY                (1u << 31)
#define SD_OCR_HCS                 (1u << 30)
#define SD_OCR_3V2_3V4             0x00300000u
#define SD_IF_COND_ARG             0x000001aau
#define SD_RCA_ARG(rca)            ((uint32_t)(rca) << 16)
#define SD_STATUS_READY_FOR_DATA   (1u << 8)
#define SD_STATUS_CURRENT_STATE(v) (((v) >> 9) & 0xfu)
#define SD_STATE_TRAN              4u

#define SDHCI_WAIT_LIMIT           1000000u
#define SDHCI_ACMD41_RETRIES       200u

typedef struct sdhci_platform {
    const char *name;
    uint64_t base;
    uint32_t base_clock_hz;
    uint32_t init_clock_hz;
    uint32_t transfer_clock_hz;
    int (*platform_setup)(void);
} sdhci_platform_t;

typedef struct sdhci_device {
    const sdhci_platform_t *platform;
    uint32_t rca;
    uint8_t initialized;
    uint8_t high_capacity;
    block_device_info_t info;
} sdhci_device_t;

#if defined(PLATFORM_QEMU)
static int qemu_sdhci_setup(void);

static const sdhci_platform_t active_platform = {
    .name = "qemu-raspi3b-sdhci",
    .base = UINT64_C(0x3f300000),
    .base_clock_hz = 52000000u,
    .init_clock_hz = 400000u,
    .transfer_clock_hz = 400000u,
    .platform_setup = qemu_sdhci_setup,
};
#else
static int rpi5_sdhci_setup(void);

static const sdhci_platform_t rpi5_platform = {
    .name = "rpi5-bcm2712-sdio1",
    .base = RPI5_BCM2712_SDIO1_BASE,
    .base_clock_hz = 50000000u,
    .init_clock_hz = 400000u,
    .transfer_clock_hz = 400000u,
    .platform_setup = rpi5_sdhci_setup,
};
#define active_platform rpi5_platform
#endif

static sdhci_device_t sd = {
    .platform = &active_platform,
    .rca = 0,
    .initialized = 0,
    .high_capacity = 0,
    .info = {
        .sector_count = 0,
        .sector_size = SD_BLOCK_SIZE_BYTES,
        .name = "rpi5-bcm2712-sdio1",
    },
};

static uint32_t reg_read32(uint32_t reg) {
    return rpi5_mmio_read32(sd.platform->base + reg);
}

static void reg_write32(uint32_t reg, uint32_t value) {
    rpi5_mmio_write32(sd.platform->base + reg, value);
}

static uint16_t reg_read16(uint32_t reg) {
    return *(volatile uint16_t *)(uintptr_t)rpi5_mmio_va(sd.platform->base + reg);
}

static void reg_write16(uint32_t reg, uint16_t value) {
    *(volatile uint16_t *)(uintptr_t)rpi5_mmio_va(sd.platform->base + reg) = value;
}

static uint8_t reg_read8(uint32_t reg) {
    return *(volatile uint8_t *)(uintptr_t)rpi5_mmio_va(sd.platform->base + reg);
}

static void reg_write8(uint32_t reg, uint8_t value) {
    *(volatile uint8_t *)(uintptr_t)rpi5_mmio_va(sd.platform->base + reg) = value;
}

static void log_hex(const char *label, uint64_t value) {
    uart_puts(label);
    uart_puthex(value);
    uart_puts("\n");
}

static void log_command_failure(uint32_t index) {
    uart_puts("[sdhci] command failed cmd=");
    uart_puthex(index);
    uart_puts(" status=");
    uart_puthex(reg_read32(SDHCI_INT_STATUS));
    uart_puts(" present=");
    uart_puthex(reg_read32(SDHCI_PRESENT_STATE));
    uart_puts(" command=");
    uart_puthex(reg_read16(SDHCI_COMMAND));
    uart_puts(" clock=");
    uart_puthex(reg_read16(SDHCI_CLOCK_CONTROL));
    uart_puts("\n");
}

static void log_transfer_state(const char *label) {
    uart_puts(label);
    uart_puts(" status=");
    uart_puthex(reg_read32(SDHCI_INT_STATUS));
    uart_puts(" present=");
    uart_puthex(reg_read32(SDHCI_PRESENT_STATE));
    uart_puts(" transfer=");
    uart_puthex(reg_read16(SDHCI_TRANSFER_MODE));
    uart_puts(" count=");
    uart_puthex(reg_read16(SDHCI_BLOCK_COUNT));
    uart_puts("\n");
}

static int wait_clear32(uint32_t reg, uint32_t mask) {
    for (uint32_t i = 0; i < SDHCI_WAIT_LIMIT; i++) {
        if ((reg_read32(reg) & mask) == 0) {
            return 0;
        }
        asm volatile("yield" ::: "memory");
    }
    return -1;
}

static int wait_int_status(uint32_t mask) {
    for (uint32_t i = 0; i < SDHCI_WAIT_LIMIT; i++) {
        uint32_t value = reg_read32(SDHCI_INT_STATUS);
        if ((value & SDHCI_INT_ERROR) != 0) {
            return -2;
        }
        if ((value & mask) == mask) {
            return 0;
        }
        asm volatile("yield" ::: "memory");
    }
    return -1;
}

static int wait_buffer_ready(uint32_t interrupt_mask, uint32_t present_mask) {
    for (uint32_t i = 0; i < SDHCI_WAIT_LIMIT; i++) {
        uint32_t status = reg_read32(SDHCI_INT_STATUS);
        if ((status & SDHCI_INT_ERROR) != 0) {
            return -2;
        }
        if ((status & interrupt_mask) != 0 ||
            (reg_read32(SDHCI_PRESENT_STATE) & present_mask) != 0) {
            return 0;
        }
        asm volatile("yield" ::: "memory");
    }

    return -1;
}

static int wait_reset_done(uint8_t mask) {
    for (uint32_t i = 0; i < SDHCI_WAIT_LIMIT; i++) {
        if ((reg_read8(SDHCI_SOFTWARE_RESET) & mask) == 0) {
            return 0;
        }
        asm volatile("yield" ::: "memory");
    }
    return -1;
}

static void reset_command_line(void) {
    reg_write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_CMD);
    (void)wait_reset_done(SDHCI_RESET_CMD);
    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
}

static void reset_data_line(void) {
    reg_write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
    (void)wait_reset_done(SDHCI_RESET_DATA);
    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
}

static uint16_t clock_divider(uint32_t base_hz, uint32_t target_hz) {
    if (target_hz == 0 || target_hz >= base_hz) {
        return 0;
    }

    uint32_t div = (base_hz + target_hz - 1u) / target_hz;
    uint32_t power = 1;

    while (power < div && power < 2046u) {
        power <<= 1;
    }

    if (power < 2u) {
        power = 2u;
    }

    div = power >> 1;
    if (div > 0x3ffu) {
        div = 0x3ffu;
    }

    return (uint16_t)(((div & 0xffu) << 8) | ((div & 0x300u) >> 2));
}

static int set_clock(uint32_t target_hz) {
    uint16_t control = reg_read16(SDHCI_CLOCK_CONTROL);

    control &= (uint16_t)~SDHCI_CLOCK_SD_ENABLE;
    reg_write16(SDHCI_CLOCK_CONTROL, control);

    control &= (uint16_t)~0xffc0u;
    control |= clock_divider(sd.platform->base_clock_hz, target_hz);
    control |= SDHCI_CLOCK_INT_ENABLE;

    reg_write8(SDHCI_TIMEOUT_CONTROL, SDHCI_TIMEOUT_MAX);
    reg_write16(SDHCI_CLOCK_CONTROL, control);

    for (uint32_t i = 0; i < SDHCI_WAIT_LIMIT; i++) {
        if ((reg_read16(SDHCI_CLOCK_CONTROL) & SDHCI_CLOCK_INT_STABLE) != 0) {
            reg_write16(SDHCI_CLOCK_CONTROL,
                        reg_read16(SDHCI_CLOCK_CONTROL) | SDHCI_CLOCK_SD_ENABLE);
            timer_delay_ms(2);
            return 0;
        }
        asm volatile("yield" ::: "memory");
    }

    return -1;
}

static uint16_t make_command(uint32_t index, uint32_t response, uint32_t flags) {
    return SD_CMD_INDEX(index) |
           (uint16_t)(response & SD_CMD_RESP_MASK) |
           (uint16_t)flags;
}

static void set_block_transfer(uint16_t block_size, uint16_t block_count) {
    reg_write16(SDHCI_BLOCK_SIZE_REG, block_size);
    reg_write16(SDHCI_BLOCK_COUNT, block_count);
}

static int send_command(uint32_t index, uint32_t arg, uint32_t response,
                        uint32_t flags, uint16_t transfer_mode) {
    uint32_t inhibit = SDHCI_STATE_CMD_INHIBIT;
    if ((flags & SD_CMD_DATA_PRESENT) != 0) {
        inhibit |= SDHCI_STATE_DAT_INHIBIT;
    }

    if (wait_clear32(SDHCI_PRESENT_STATE, inhibit) != 0) {
        uart_puts("[sdhci] timeout waiting for inhibit clear\n");
        log_command_failure(index);
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    reg_write16(SDHCI_TRANSFER_MODE, transfer_mode);
    reg_write32(SDHCI_ARGUMENT, arg);
    reg_write16(SDHCI_COMMAND, make_command(index, response, flags));

    if (wait_int_status(SDHCI_INT_CMD_COMPLETE) != 0) {
        log_command_failure(index);
        reset_command_line();
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_CMD_COMPLETE);
    return 0;
}

static int send_app_command(uint32_t acmd, uint32_t arg, uint32_t response,
                            uint32_t flags, uint16_t transfer_mode) {
    if (send_command(55, SD_RCA_ARG(sd.rca), SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        return -1;
    }

    return send_command(acmd, arg, response, flags, transfer_mode);
}

static int wait_card_ready_for_data(void) {
    for (uint32_t i = 0; i < 1000u; i++) {
        if (send_command(13, SD_RCA_ARG(sd.rca), SD_CMD_RESP_48,
                         SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
            return -1;
        }

        uint32_t status = reg_read32(SDHCI_RESPONSE0);
        if ((status & SD_STATUS_READY_FOR_DATA) != 0 &&
            SD_STATUS_CURRENT_STATE(status) == SD_STATE_TRAN) {
            return 0;
        }

        timer_delay_ms(1);
    }

    uart_puts("[sdhci] card did not return to transfer state\n");
    log_hex("[sdhci] card status=", reg_read32(SDHCI_RESPONSE0));
    return -1;
}

static int stop_transmission(void) {
    if (send_command(12, 0, SD_CMD_RESP_48_BUSY,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        uart_puts("[sdhci] CMD12 stop failed\n");
        reset_data_line();
        return -1;
    }

    return wait_card_ready_for_data();
}

static int reset_controller(void) {
    reg_write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
    if (wait_reset_done(SDHCI_RESET_ALL) != 0) {
        uart_puts("[sdhci] reset timed out\n");
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    reg_write32(SDHCI_INT_ENABLE, SDHCI_INT_ALL);
    reg_write32(SDHCI_SIGNAL_ENABLE, 0);
    reg_write8(SDHCI_POWER_CONTROL, SDHCI_POWER_3V3);
    timer_delay_ms(10);
    return 0;
}

static int read_scr(void) {
    uint32_t scr[2] = {0, 0};

    set_block_transfer(8u, 1u);
    if (send_app_command(51, 0, SD_CMD_RESP_48,
                         SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK | SD_CMD_DATA_PRESENT,
                         SD_TM_READ) != 0) {
        return -1;
    }

    if (wait_int_status(SDHCI_INT_BUF_READ_READY) != 0) {
        return -1;
    }

    scr[0] = reg_read32(SDHCI_BUFFER_DATA);
    scr[1] = reg_read32(SDHCI_BUFFER_DATA);

    if (wait_int_status(SDHCI_INT_TRANSFER_DONE) != 0) {
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    log_hex("[sdhci] SCR0=", scr[0]);
    log_hex("[sdhci] SCR1=", scr[1]);
    return 0;
}

static int init_card(void) {
    uint8_t v2_card = 1;

    if (send_command(0, 0, SD_CMD_RESP_NONE, 0, 0) != 0) {
        return -1;
    }
    timer_delay_ms(2);

    if (send_command(8, SD_IF_COND_ARG, SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        uart_puts("[sdhci] CMD8 timed out; trying legacy SD init\n");
        v2_card = 0;
    } else {
        uint32_t cmd8 = reg_read32(SDHCI_RESPONSE0);
        log_hex("[sdhci] CMD8=", cmd8);
        if ((cmd8 & 0xfffu) != SD_IF_COND_ARG) {
            uart_puts("[sdhci] CMD8 pattern mismatch\n");
            return -1;
        }
    }

    uint32_t ocr = 0;
    for (uint32_t i = 0; i < SDHCI_ACMD41_RETRIES; i++) {
        uint32_t acmd41_arg = SD_OCR_3V2_3V4 | (v2_card ? SD_OCR_HCS : 0);
        if (send_app_command(41, acmd41_arg, SD_CMD_RESP_48, 0, 0) != 0) {
            return -1;
        }

        ocr = reg_read32(SDHCI_RESPONSE0);
        if ((ocr & SD_OCR_BUSY) != 0) {
            break;
        }
        timer_delay_ms(10);
    }

    log_hex("[sdhci] OCR=", ocr);
    if ((ocr & SD_OCR_BUSY) == 0) {
        uart_puts("[sdhci] ACMD41 timed out\n");
        return -1;
    }
    sd.high_capacity = (ocr & SD_OCR_HCS) != 0;

    if (send_command(2, 0, SD_CMD_RESP_136, SD_CMD_CRC_CHECK, 0) != 0) {
        return -1;
    }

    if (send_command(3, 0, SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        return -1;
    }
    sd.rca = (reg_read32(SDHCI_RESPONSE0) >> 16) & 0xffffu;
    log_hex("[sdhci] RCA=", sd.rca);

    if (send_command(7, SD_RCA_ARG(sd.rca), SD_CMD_RESP_48_BUSY,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        return -1;
    }

    reg_write8(SDHCI_HOST_CONTROL1, reg_read8(SDHCI_HOST_CONTROL1) & (uint8_t)~SDHCI_HOST_4BIT);
    uart_puts("[sdhci] using 1-bit bus for PIO milestone\n");

    if (send_command(16, SD_BLOCK_SIZE_BYTES, SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
        return -1;
    }

    (void)read_scr();
    return 0;
}

#if !defined(PLATFORM_QEMU)
static void rpi5_aon_gpio_set_output(unsigned pin, uint32_t high) {
    uint32_t mask = 1u << pin;
    uint32_t iodir = rpi5_mmio_read32(RPI5_AON_GPIO_IODIR);
    uint32_t data = rpi5_mmio_read32(RPI5_AON_GPIO_DATA);

    iodir &= ~mask;
    if (high) {
        data |= mask;
    } else {
        data &= ~mask;
    }

    rpi5_mmio_write32(RPI5_AON_GPIO_DATA, data);
    rpi5_mmio_write32(RPI5_AON_GPIO_IODIR, iodir);
    rpi5_mmio_barrier();
}

static uint32_t rpi5_aon_gpio_read(unsigned pin) {
    return (rpi5_mmio_read32(RPI5_AON_GPIO_DATA) >> pin) & 1u;
}

static void rpi5_aon_gpio_set_input(unsigned pin) {
    uint32_t mask = 1u << pin;
    rpi5_mmio_write32(RPI5_AON_GPIO_IODIR,
                      rpi5_mmio_read32(RPI5_AON_GPIO_IODIR) | mask);
    rpi5_mmio_barrier();
}

static int rpi5_sdhci_setup(void) {
    uart_puts("[sdhci] power-cycling Pi 5 SD slot\n");

    rpi5_aon_gpio_set_output(RPI5_AON_SD_IOVDD_SEL, 0);
    rpi5_aon_gpio_set_output(RPI5_AON_SD_PWR_ON, 0);
    timer_delay_ms(100);
    rpi5_aon_gpio_set_output(RPI5_AON_SD_PWR_ON, 1);
    timer_delay_ms(250);

    rpi5_aon_gpio_set_input(RPI5_AON_SD_CDET_N);
    uart_puts("[sdhci] card-detect-n=");
    uart_puthex(rpi5_aon_gpio_read(RPI5_AON_SD_CDET_N));
    uart_puts("\n");

    rpi5_mmio_barrier();
    return 0;
}
#endif

#if defined(PLATFORM_QEMU)
static int qemu_sdhci_setup(void) {
    uart_puts("[sdhci] qemu raspi3b setup: using emulated SDHCI slot\n");
    return 0;
}
#endif

int sdhci_block_init(void) {
    if (sd.initialized) {
        return 0;
    }

    uart_puts("[sdhci] init ");
    uart_puts(sd.platform->name);
    uart_puts(" base=");
    uart_puthex(sd.platform->base);
    uart_puts("\n");

    if (sd.platform->platform_setup != NULL && sd.platform->platform_setup() != 0) {
        return -1;
    }

    log_hex("[sdhci] capabilities=", reg_read32(SDHCI_CAPABILITIES));

    if (reset_controller() != 0) {
        return -1;
    }
    uart_puts("[sdhci] reset complete\n");

    if (set_clock(sd.platform->init_clock_hz) != 0) {
        uart_puts("[sdhci] init clock failed\n");
        return -1;
    }
    uart_puts("[sdhci] init clock enabled\n");

    if (init_card() != 0) {
        uart_puts("[sdhci] card init failed\n");
        return -1;
    }

    if (set_clock(sd.platform->transfer_clock_hz) != 0) {
        uart_puts("[sdhci] transfer clock failed\n");
        return -1;
    }

    sd.initialized = 1;
    uart_puts("[sdhci] card ready\n");
    return 0;
}

int sdhci_block_read(uint64_t lba, uint32_t count, void *buf) {
    if (!sd.initialized || buf == NULL || count == 0 || count > UINT16_MAX ||
        lba > UINT32_MAX) {
        return -1;
    }

    uint32_t arg = sd.high_capacity ? (uint32_t)lba : (uint32_t)(lba * SD_BLOCK_SIZE_BYTES);
    uint32_t *dst = (uint32_t *)buf;
    uint32_t command = (count == 1) ? 17u : 18u;
    uint16_t transfer_mode = SD_TM_READ;
    if (count > 1) {
        transfer_mode |= SD_TM_BLOCK_COUNT_ENABLE | SD_TM_MULTI_BLOCK;
    }

    set_block_transfer(SD_BLOCK_SIZE_BYTES, (uint16_t)count);
    if (send_command(command, arg, SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK | SD_CMD_DATA_PRESENT,
                     transfer_mode) != 0) {
        return -1;
    }

    for (uint32_t block = 0; block < count; block++) {
        if (wait_buffer_ready(SDHCI_INT_BUF_READ_READY, SDHCI_STATE_READ_READY) != 0) {
            log_transfer_state("[sdhci] read wait failed");
            reset_data_line();
            return -1;
        }

        for (uint32_t i = 0; i < SD_WORDS_PER_BLOCK; i++) {
            dst[(block * SD_WORDS_PER_BLOCK) + i] = reg_read32(SDHCI_BUFFER_DATA);
        }

        reg_write32(SDHCI_INT_STATUS, SDHCI_INT_BUF_READ_READY);
    }

    if (wait_int_status(SDHCI_INT_TRANSFER_DONE) != 0) {
        log_transfer_state("[sdhci] read done failed");
        reset_data_line();
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    if (count > 1) {
        return stop_transmission();
    }

    return wait_card_ready_for_data();
}

int sdhci_block_write(uint64_t lba, uint32_t count, const void *buf) {
    if (!sd.initialized || buf == NULL || count == 0 || count > UINT16_MAX ||
        lba > UINT32_MAX) {
        return -1;
    }

    uint32_t arg = sd.high_capacity ? (uint32_t)lba : (uint32_t)(lba * SD_BLOCK_SIZE_BYTES);
    const uint32_t *src = (const uint32_t *)buf;
    uint32_t command = (count == 1) ? 24u : 25u;
    uint16_t transfer_mode = 0;
    if (count > 1) {
        transfer_mode |= SD_TM_BLOCK_COUNT_ENABLE | SD_TM_MULTI_BLOCK;
        if (send_app_command(23, count, SD_CMD_RESP_48,
                             SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK, 0) != 0) {
            return -1;
        }
    }

    set_block_transfer(SD_BLOCK_SIZE_BYTES, (uint16_t)count);
    if (send_command(command, arg, SD_CMD_RESP_48,
                     SD_CMD_CRC_CHECK | SD_CMD_INDEX_CHECK | SD_CMD_DATA_PRESENT,
                     transfer_mode) != 0) {
        return -1;
    }

    for (uint32_t block = 0; block < count; block++) {
        if (wait_buffer_ready(SDHCI_INT_BUF_WRITE_READY, SDHCI_STATE_WRITE_READY) != 0) {
            log_transfer_state("[sdhci] write wait failed");
            reset_data_line();
            return -1;
        }

        for (uint32_t i = 0; i < SD_WORDS_PER_BLOCK; i++) {
            reg_write32(SDHCI_BUFFER_DATA, src[(block * SD_WORDS_PER_BLOCK) + i]);
        }

        reg_write32(SDHCI_INT_STATUS, SDHCI_INT_BUF_WRITE_READY);
    }

    if (wait_int_status(SDHCI_INT_TRANSFER_DONE) != 0) {
        log_transfer_state("[sdhci] write done failed");
        reset_data_line();
        return -1;
    }

    reg_write32(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    if (count > 1 && stop_transmission() != 0) {
        return -1;
    }

    return wait_card_ready_for_data();
}

const block_device_info_t *sdhci_block_get_info(void) {
    return &sd.info;
}
