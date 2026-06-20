#pragma once

#include <stdint.h>
#include "string.h"
#include "types.h"

// Values for the `type` field of inode metadata.
#define DIRECTORY_TYPE 0
#define FILE_TYPE 1
#define SYMLINK_TYPE 2
#define CHAR_DRIVER_TYPE 3
#define PIPE_TYPE 4

#include "oft.h"
#include "fs/caches/lru_cache.h"
#include "errors.h"

// ============================================================
// FAT/INODE WRAPPER FUNCTIONS
// These dispatch to the appropriate FAT or inode implementation
// based on the currently mounted filesystem's type.
// ============================================================

/**
 * @brief Create a fresh filesystem in the block-device region after
 * the boot partition. Writes the superblock, scalable bitmaps, inode
 * table, and root directory.
 *
 * @param inode_table_blocks Number of blocks reserved for the inode table.
 * @param block_size_config Block size configuration (0-4), mapped
 * to a concrete bytes-per-block value internally.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t mkfs(int inode_table_blocks, int block_size_config);

/**
 * @brief Mount the filesystem in the configured block-device region.
 * Reads and validates the superblock, populates module-level layout
 * state, and initializes runtime filesystem state.
 *
 * @return SUCCESS on success, FS_ALREADY_MOUNTED if something is
 * already mounted, FS_INVALID if the region does not contain this
 * filesystem, or another negative error code on failure.
 */
err_t mount(void);

/**
 * @brief Unmount the currently mounted filesystem. Syncs any
 * in-memory state to disk, closes the backing file, and clears
 * module-level state.
 *
 * @return SUCCESS on success, FS_NOT_MOUNTED if nothing is mounted,
 * or a negative error code on failure.
 */
err_t unmount();

err_t fs_set_block_region(uint64_t base_block, uint64_t block_count);

uint64_t fs_get_base_block(void);

uint64_t fs_get_block_count(void);

err_t fs_set_layout(uint32_t bytes_per_block_value, uint32_t total_blocks,
                    uint32_t block_bitmap_start, uint32_t block_bitmap_blocks,
                    uint32_t inode_bitmap_start, uint32_t inode_bitmap_blocks,
                    uint32_t inode_table_start, uint32_t inode_table_blocks,
                    uint32_t data_start_block);

/**
 * @brief Get the (relative) block number of the block at position
 * block_num within the file described by entry.
 *
 * @param entry OFT entry identifying the file.
 * @param block_num Index of the block within the file (0 = first).
 * @return Relative block number, or 0 if the index is past EOF.
 */
block_no_t get_ith_block_of_file(struct oft_entry *entry, unsigned int block_num);

/**
 * @brief Get the absolute block number at index next_block_index
 * of a file, using prev_block_num as a starting hint.
 *
 * @param id_in_fs File id (first FAT block / inode id).
 * @param next_block_index Absolute block index within the file.
 * @param prev_block_num Previous block's absolute number (used as
 * a walk starting point for FAT chains).
 * @return Absolute block number, or 0 if no such block exists.
 */
block_no_t get_ith_block_of_file_with_prev(ino_id_t id_in_fs, int next_block_index, block_no_t prev_block_num);

/**
 * @brief Free all blocks belonging to the file named f_name,
 * removing its dirent and releasing its storage.
 *
 * @param f_name Name of the file to free.
 * @return SUCCESS on success, FILE_NOT_FOUND if no such file,
 * or a negative error code on failure.
 */
err_t free_file(const char* f_name);

/**
 * @brief Allocate a fresh file of the given type and populate its
 * OFT entry (first block / inode id and metadata).
 *
 * @param entry Output: receives a pointer to the new OFT entry.
 * @param file_type One of the *_TYPE constants.
 * @param perm Initial permission bits for the inode.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t add_new_file(struct oft_entry **entry, int file_type, uint8_t perm);

/**
 * @brief Allocate a new file and return just its first-block
 * identifier, without creating an OFT entry. Intended for callers
 * that manage the OFT on their own.
 *
 * @param new_block Output: receives the first block / inode id of
 * the new file.
 * @param file_type One of the *_TYPE constants.
 * @param perm Initial permission bits for the inode.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t add_new_file_with_id(block_no_t* new_block, int file_type, uint8_t perm);

// ============================================================
// HELPER FUNCTIONS
// ============================================================

/**
 * @brief Find the first free bit in the bitmap stored in the block
 * at block_with_bitmap and return its index. Optionally marks the
 * bit as taken to atomically allocate it.
 *
 * Note: the returned index is zero-indexed.
 *
 * @param free_block Output: receives the index of the free bit.
 * @param block_with_bitmap Absolute block number holding the bitmap.
 * @param update_taken If 1, flip the located bit to taken before
 * returning (i.e. allocate it); if 0, only peek.
 * @return SUCCESS on success, NO_FREE_BLOCKS if the bitmap is full,
 * or a negative error code on failure.
 */
err_t find_free_from_bitmap(uint32_t *free_block, block_no_t block_with_bitmap, int update_taken);
err_t find_free_from_bitmap_range(uint32_t *free_block, block_no_t bitmap_start,
                                  uint32_t bitmap_blocks, uint32_t valid_bits,
                                  int update_taken);
err_t set_bit_in_bitmap_range(block_no_t bitmap_start, uint32_t bitmap_blocks,
                              uint32_t valid_bits, uint32_t bit_idx,
                              int set_bit);

/**
 * @brief Write bytes_per_block bytes from data into the block
 * numbered num on disk.
 *
 * @param data Pointer to a buffer of at least bytes_per_block bytes.
 * @param num Absolute block number to write to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t write_block(void *data, block_no_t num);

/**
 * @brief Read bytes_per_block bytes from the block numbered num on
 * disk into the caller-provided buffer.
 *
 * @param data Pointer to a buffer of at least bytes_per_block bytes.
 * @param num Absolute block number to read from.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t read_block(void *data, block_no_t num);

/**
 * @brief Flip the bit at position idx in a bitmap held in memory.
 * Pure in-memory helper; does not write to disk.
 *
 * @param data Pointer to the bitmap bytes.
 * @param idx Bit position to toggle.
 */
void toggle_in_bitmap_data(unsigned char *data, unsigned int idx);

/**
 * @brief Get the absolute block number of the first data block of
 * the file with the given id.
 *
 * @param file_id File id (first FAT block / inode id).
 * @return Absolute block number of the file's first data block.
 */
block_no_t get_first_block(ino_id_t file_id);

/**
 * @brief Given the current relative block number and an index,
 * return the next relative block in the file's chain.
 *
 * @param entry OFT entry identifying the file.
 * @param curr_block_no Current relative block number.
 * @param next_block_index Index of the desired next block.
 * @return Relative block number of the next block, or 0 if none.
 */
block_no_t get_next_block_from_file(struct oft_entry *entry, int next_block_index);

/**
 * @brief Allocate a new block and append it to the file described
 * by entry, returning its relative block number.
 *
 * @param entry OFT entry of the file to extend.
 * @param block_num Output: receives the relative block number of
 * the newly allocated block.
 * @return SUCCESS on success, FAT_NO_SPACE_REMAINING / NO_FREE_BLOCKS
 * if the filesystem is full, or a negative error code otherwise.
 */
err_t allocate_new_block_for_file(struct oft_entry *entry, block_no_t *block_num);

/**
 * @brief Allocate a new block and append it to the file identified
 * by id_in_fs, returning its absolute block number.
 *
 * @param prev_block Relative block number of the current last block.
 * @param id_in_fs File id.
 * @param allocated_block Output: receives the absolute block number
 * of the newly allocated block.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t allocate_new_block_for_file_from_id(ino_id_t id_in_fs, block_no_t* allocated_block);

/**
 * @brief Allocate a new block for a file given the previous block's
 * relative number, returning the new block's relative number.
 *
 * @param entry OFT entry of the file being extended.
 * @param prev_block_no Relative block number of the current last block.
 * @param new_block Output: receives the relative block number of
 * the new block.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t allocate_new_block_for_file_with_prev(struct oft_entry *entry, block_no_t prev_block_no, block_no_t *new_block);

/**
 * @brief Get the file size in bytes of the file associated with entry.
 *
 * @param entry OFT entry identifying the file.
 * @return File size in bytes, or a negative error code on failure.
 */
int get_file_size(struct oft_entry *entry);

/**
 * @brief Free every block of the file except its first, leaving the
 * file's identity intact but truncating its contents. Used for
 * implementing F_WRITE (truncate) open semantics.
 *
 * @param entry OFT entry of the file to truncate.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t clear_blocks_of_file(struct oft_entry *entry);

/**
 * @brief Free only the last block of the file identified by id_in_fs.
 * Used by k_write / k_lseek when the file shrinks past a block boundary.
 *
 * @param id_in_fs File id.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t remove_last_block(ino_id_t id_in_fs);

// Accessors for module-level mount state. Most of these are valid
// only while a filesystem is mounted.
int get_num_table_blocks();
int get_bytes_per_block();
uint32_t get_total_fs_blocks();
uint32_t get_block_bitmap_start();
uint32_t get_block_bitmap_blocks();
uint32_t get_inode_bitmap_start();
uint32_t get_inode_bitmap_blocks();
uint32_t get_inode_table_start();
uint32_t get_data_start_block();
int get_file_system_type();
int get_is_mounted();
ino_id_t get_curr_dir();

/**
 * @brief Change the current working directory to the directory
 * identified by id. Used by k_change_directory.
 *
 * @param id Inode id / first block of the new cwd.
 */
void set_curr_dir(ino_id_t id);

int update_file_size(struct oft_entry *entry, int new_size);

block_no_t get_ith_block_of_file_by_id(ino_id_t id, unsigned int block_num);

int get_file_size_by_id(ino_id_t ino_id);

int clear_blocks_of_file_by_id(ino_id_t id);
