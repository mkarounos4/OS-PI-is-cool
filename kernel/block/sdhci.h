#pragma once

#include <stdint.h>

#include "block.h"

int sdhci_block_init(void);
int sdhci_block_read(uint64_t lba, uint32_t count, void *buf);
int sdhci_block_write(uint64_t lba, uint32_t count, const void *buf);
const block_device_info_t *sdhci_block_get_info(void);
