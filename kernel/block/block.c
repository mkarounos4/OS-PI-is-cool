#include "block.h"

#include "sdhci.h"

int block_init(void) {
    return sdhci_block_init();
}

int block_read(uint64_t lba, uint32_t count, void *buf) {
    return sdhci_block_read(lba, count, buf);
}

int block_write(uint64_t lba, uint32_t count, const void *buf) {
    return sdhci_block_write(lba, count, buf);
}

const block_device_info_t *block_get_info(void) {
    return sdhci_block_get_info();
}
