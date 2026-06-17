#include "fs_test.h"

#include <stdint.h>

#include "disk/block.h"
#include "disk.h"
#include "errors.h"
#include "inodes.h"
#include "uart/uart.h"

#define FS_TEST_INODE_TABLE_BLOCKS 16
#define FS_TEST_BLOCK_SIZE_CONFIG 1
#define FS_TEST_BLOCK_BUFFER_SIZE 4096

static unsigned char fs_test_block[FS_TEST_BLOCK_BUFFER_SIZE] __attribute__((aligned(16)));

static void fs_test_print_hex(const char *label, uint64_t value) {
    uart_puts(label);
    uart_puthex(value);
    uart_puts("\n");
}

static int signature_matches(const struct superblock_st *superblock) {
    for (int i = 0; i < FS_SIGNATURE_SIZE; i++) {
        if (superblock->signature[i] != FS_SIGNATURE[i]) {
            return 0;
        }
    }
    return 1;
}

static int check_superblock_metadata(void) {
    uint64_t base_block = fs_get_base_block();
    uint64_t block_count = fs_get_block_count();
    uint32_t driver_block_size = block_get_size();

    if (driver_block_size == 0 || driver_block_size > FS_TEST_BLOCK_BUFFER_SIZE) {
        uart_puts("[fs-test] invalid driver block size\n");
        return -1;
    }

    if (block_read(base_block, 1, fs_test_block) != 0) {
        uart_puts("[fs-test] failed to read superblock\n");
        return -1;
    }

    const struct superblock_st *superblock = (const struct superblock_st *)fs_test_block;
    if (!signature_matches(superblock)) {
        uart_puts("[fs-test] signature mismatch\n");
        return -1;
    }

    if (superblock->bytes_per_block != driver_block_size ||
        superblock->total_blocks != block_count ||
        superblock->block_bitmap_blocks == 0 ||
        superblock->inode_bitmap_blocks == 0 ||
        superblock->inode_table_blocks == 0 ||
        superblock->data_start_block >= superblock->total_blocks ||
        superblock->root_inode_id != ROOT_INO) {
        uart_puts("[fs-test] superblock metadata mismatch\n");
        fs_test_print_hex("[fs-test] bytes_per_block=", superblock->bytes_per_block);
        fs_test_print_hex("[fs-test] total_blocks=", superblock->total_blocks);
        fs_test_print_hex("[fs-test] data_start=", superblock->data_start_block);
        return -1;
    }

    return 0;
}

int fs_test_mkfs_metadata(int inode_table_blocks, int block_size_config) {
    uart_puts("[fs-test] begin mkfs metadata\n");
    uart_puts("[fs-test] WARNING: mkfs formats the post-boot disk region\n");

    err_t err = mkfs(inode_table_blocks, block_size_config);
    if (err != SUCCESS) {
        fs_test_print_hex("[fs-test] mkfs failed err=", (uint64_t)(uint32_t)err);
        return -1;
    }

    if (check_superblock_metadata() != 0) {
        uart_puts("[fs-test] FAIL mkfs metadata\n");
        return -1;
    }

    uart_puts("[fs-test] PASS mkfs metadata\n");
    return 0;
}

static int test_bitmap_boundary(block_no_t bitmap_start,
                                uint32_t bitmap_blocks,
                                uint32_t valid_bits,
                                uint32_t first_safe_bit,
                                const char *label) {
    uint32_t bits_per_block = (uint32_t)get_bytes_per_block() * 8u;
    uint32_t bit = bits_per_block;
    if (bit < first_safe_bit) {
        bit = first_safe_bit;
    }

    if (bitmap_blocks < 2 || bit >= valid_bits ||
        (bit / bits_per_block) >= bitmap_blocks) {
        uart_puts(label);
        uart_puts(" SKIP boundary not present\n");
        return 0;
    }

    err_t err = set_bit_in_bitmap_range(bitmap_start, bitmap_blocks, valid_bits, bit, 1);
    if (err != SUCCESS) {
        uart_puts(label);
        uart_puts(" failed to set boundary bit\n");
        return -1;
    }

    err = set_bit_in_bitmap_range(bitmap_start, bitmap_blocks, valid_bits, bit, 0);
    if (err != SUCCESS) {
        uart_puts(label);
        uart_puts(" failed to clear boundary bit\n");
        return -1;
    }

    uart_puts(label);
    uart_puts(" PASS boundary set/clear\n");
    return 0;
}

int fs_test_bitmap_boundaries(void) {
    uart_puts("[fs-test] begin bitmap boundaries\n");

    block_no_t free_block;
    err_t err = find_free_block(&free_block, 0);
    if (err != SUCCESS || free_block <= get_data_start_block()) {
        uart_puts("[fs-test] invalid first free block\n");
        return -1;
    }

    ino_id_t free_inode;
    err = find_free_inode(&free_inode, 0);
    if (err != SUCCESS || free_inode <= ROOT_INO) {
        uart_puts("[fs-test] invalid first free inode\n");
        return -1;
    }

    if (test_bitmap_boundary(get_block_bitmap_start(),
                             get_block_bitmap_blocks(),
                             get_total_fs_blocks(),
                             get_data_start_block() + 1u,
                             "[fs-test] block bitmap") != 0) {
        return -1;
    }

    uint32_t inode_capacity = (uint32_t)get_num_table_blocks() *
                              (uint32_t)INODES_PER_BLOCK;
    if (test_bitmap_boundary(get_inode_bitmap_start(),
                             get_inode_bitmap_blocks(),
                             inode_capacity,
                             ROOT_INO,
                             "[fs-test] inode bitmap") != 0) {
        return -1;
    }

    uart_puts("[fs-test] PASS bitmap boundaries\n");
    return 0;
}

void fs_test_run_mkfs_smoke(void) {
    if (fs_test_mkfs_metadata(FS_TEST_INODE_TABLE_BLOCKS,
                              FS_TEST_BLOCK_SIZE_CONFIG) != 0) {
        uart_puts("[fs-test] FAIL smoke\n");
        return;
    }

    if (fs_test_bitmap_boundaries() != 0) {
        uart_puts("[fs-test] FAIL smoke\n");
        return;
    }

    uart_puts("[fs-test] PASS smoke\n");
}
