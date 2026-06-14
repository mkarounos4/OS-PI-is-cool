#pragma once

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

typedef uint16_t ino_id_t;
typedef uint16_t block_no_t;

// Attributes for inodes
typedef struct attributes_t_struct {
    uint16_t i_links_count; // Num dir entries pointing to this (deletes inode when erach 0)
    uint32_t i_size;
    uint32_t i_blocks; // Num blocks
} attributes_t;

// Struct for inode data
struct inode_st {
    attributes_t metadata;
    block_no_t blocks[15];
};
struct cached_inode_st;


#include "errors.h"
#include "disk.h"
#include "fs/caches/inode_cache.h"


// Constants
#define INODE_BYTE_SIZE 128
#define BLOCK_BITMAP 1
#define INODE_BITMAP 2
#define INODE_TABLE_START_BLOCK 3
#define ROOT_INO 1
#define INODES_PER_BLOCK (get_bytes_per_block() / sizeof(struct inode_st))
#define BLOCKS_IN_SINGLE_PTR (get_bytes_per_block() / sizeof(block_no_t))
#define BLOCKS_IN_DOUBLE_PTR (BLOCKS_IN_SINGLE_PTR * BLOCKS_IN_SINGLE_PTR)
#define BLOCKS_IN_TRIPLE_PTR (BLOCKS_IN_DOUBLE_PTR * BLOCKS_IN_SINGLE_PTR)
#define INODES_INDICATOR 0xFFFF

#define ALL_PERMS 0

// Superblock parameters for loading/storing file system metadata
struct superblock_st {
    uint16_t inodes_indicator;
    int num_fat_inode_blocks;
    int bytes_per_block;
};

/**
 * @brief Fetch the inode with the given id, using the inode cache
 * when possible and reading from disk otherwise.
 *
 * @param node Output: receives a pointer to the cached inode entry.
 * The returned pointer is owned by the cache; do not free.
 * @param id Inode id to fetch.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t get_inode(struct cached_inode_st** node, ino_id_t id);

/**
 * @brief Write the given inode data to its on-disk slot.
 *
 * @param node Inode data to persist.
 * @param id Inode id / on-disk slot to write to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t write_inode(struct inode_st *node, ino_id_t id);

/**
 * @brief Format a file descriptor as a fresh inode-layout PennFAT
 * filesystem with blocks_in_fat inode blocks and the given block
 * size configuration.
 *
 * @param file_d Open file descriptor to the backing file.
 * @param blocks_in_fat Number of blocks reserved for the inode table.
 * @param bytes_per_block Block size configuration (0-4).
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t mkfs_inode(int file_d, int blocks_in_fat, int bytes_per_block);

/**
 * @brief Mount the inode filesystem currently open at
 * get_mounted_file_d() and report the layout back to the caller.
 *
 * @param bytes_per_block Output: bytes per block in the mounted FS.
 * @param num_inode_blocks Output: number of inode table blocks.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t mount_inode(int *bytes_per_block, int *num_inode_blocks);

/**
 * @brief Unmount the currently mounted inode filesystem, flushing
 * any dirty cache state and releasing in-memory resources.
 *
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t unmount_inode();

/**
 * @brief Locate the first free inode in the inode bitmap, optionally
 * marking it as taken.
 *
 * @param free_block Output: receives the id of the free inode.
 * @param update_taken If 1, flip the located bit to taken (allocate);
 * if 0, only peek.
 * @return SUCCESS on success, INODE_FULL if none free, or a negative
 * error code on failure.
 */
err_t find_free_inode(ino_id_t *free_block, int update_taken);

/**
 * @brief Locate the first free data block in the block bitmap,
 * optionally marking it as taken.
 *
 * @param free_block Output: receives the relative block number of
 * the free block.
 * @param update_taken If 1, flip the located bit to taken (allocate);
 * if 0, only peek.
 * @return SUCCESS on success, NO_FREE_BLOCKS if none free, or a
 * negative error code on failure.
 */
err_t find_free_block(block_no_t *free_block, int update_taken);

/**
 * @brief Get the block number at position block_num within the given
 * inode, walking through direct / indirect pointers as needed.
 *
 * @param inode Inode to index into.
 * @param block_num Zero-based block index within the file.
 * @return Block number at that position, or 0 if past EOF.
 */
block_no_t get_block_num_from_inode(struct inode_st *inode, unsigned int block_num);

/**
 * @brief Write one block of data to the block at position block_num
 * within the given inode's file.
 *
 * @param data Buffer of at least bytes_per_block bytes to write.
 * @param inode Inode whose file is being written.
 * @param block_num Zero-based block index within the file.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t write_block_of_file_inode(void *data, struct inode_st *inode, unsigned int block_num);

/**
 * @brief Read one block of data from the block at position block_num
 * within the given inode's file.
 *
 * @param data Buffer of at least bytes_per_block bytes to receive
 * the block's contents.
 * @param inode Inode whose file is being read.
 * @param block_num Zero-based block index within the file.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t get_block_from_file_inode(void *data, struct inode_st *inode, unsigned int block_num);

/**
 * @brief Allocate a new data block and append it to the file backed
 * by the given inode, updating the inode's block pointers and
 * i_blocks metadata accordingly.
 *
 * @param inode Inode of the file being extended.
 * @param returned_block_num Output: receives the block number of the
 * newly allocated block.
 * @return SUCCESS on success, NO_FREE_BLOCKS if the bitmap is full,
 * or a negative error code on failure.
 */
err_t allocate_block_for_file_inode(struct inode_st *inode, block_no_t *returned_block_num);

/**
 * @brief Allocate a new data block and append it to the file with
 * the given inode id. Same as allocate_block_for_file_inode but
 * takes an id instead of a preloaded inode pointer.
 *
 * @param id_in_fs Inode id of the file being extended.
 * @param new_block Output: receives the block number of the newly
 * allocated block.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t allocate_block_for_file_inode_from_id(ino_id_t id_in_fs, block_no_t *new_block);

/**
 * @brief Free all blocks belonging to the given cached inode and
 * clear its entries in the block and inode bitmaps.
 *
 * @param cache_inode Cached inode to release.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t free_file_inode(struct cached_inode_st *cache_inode);

/**
 * @brief Create a new inode of the given file type, marking its
 * slot as taken and initializing its metadata.
 *
 * @param inode_num Output: receives the id of the new inode.
 * @param file_type One of the *_TYPE constants from disk.h.
 * @return SUCCESS on success, INODE_FULL if no free inode slots,
 * or a negative error code on failure.
 */
err_t add_new_file_inode(ino_id_t *inode_num, int file_type);

/**
 * @brief Read an inode directly from disk, bypassing the inode cache.
 * Used when the caller needs guaranteed-fresh data or when the cache
 * is unavailable.
 *
 * @param node Output: receives the inode data.
 * @param id Inode id to read.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t get_inode_raw(struct inode_st *node, ino_id_t id);

/**
 * @brief Return the block number at position index of the file with
 * the given inode id. Convenience wrapper around get_inode +
 * get_block_num_from_inode.
 *
 * @param id Inode id of the file.
 * @param index Zero-based block index.
 * @return Block number at the requested index, or 0 if past EOF.
 */
block_no_t get_block_num_from_inode_with_id(ino_id_t id, unsigned int index);

/**
 * @brief Free every data block of the given inode. If skip_first is
 * nonzero, the first block is preserved (used to truncate a file to
 * zero bytes without losing its identity).
 *
 * @param inode Inode whose blocks should be freed.
 * @param skip_first If nonzero, leave the first block allocated.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t clear_blocks_of_inode(struct inode_st *inode, int skip_first);

/**
 * @brief Free just the last block of the file with the given inode id,
 * updating both the block bitmap and the inode's block pointers /
 * i_blocks count.
 *
 * @param id Inode id of the file.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t remove_last_block_inode(ino_id_t id);
