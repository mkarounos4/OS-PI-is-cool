#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct block_device_info {
    uint64_t sector_count;
    uint32_t sector_size;
    const char *name;
} block_device_info_t;

int block_init(void);
int block_read(uint64_t lba, uint32_t count, void *buf);
int block_write(uint64_t lba, uint32_t count, const void *buf);
const block_device_info_t *block_get_info(void);
uint64_t block_get_count(void);
uint32_t block_get_size(void);
