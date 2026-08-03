#include "block.h"

#include "sdhci.h"
#include "sync/spinlock.h"

static spinlock_t block_device_lock = SPINLOCK_INIT;

int block_init(void) {
    return sdhci_block_init();
}

int block_read(uint64_t lba, uint32_t count, void *buf) {
    uint64_t flags = spin_lock_irqsave(&block_device_lock);
    int err = sdhci_block_read(lba, count, buf);
    spin_unlock_irqrestore(&block_device_lock, flags);
    return err;
}

int block_write(uint64_t lba, uint32_t count, const void *buf) {
    uint64_t flags = spin_lock_irqsave(&block_device_lock);
    int err = sdhci_block_write(lba, count, buf);
    spin_unlock_irqrestore(&block_device_lock, flags);
    return err;
}

const block_device_info_t *block_get_info(void) {
    return sdhci_block_get_info();
}

uint64_t block_get_count(void) {
    const block_device_info_t *info = block_get_info();
    return info == NULL ? 0 : info->sector_count;
}

uint32_t block_get_size(void) {
    const block_device_info_t *info = block_get_info();
    return info == NULL ? 0 : info->sector_size;
}
