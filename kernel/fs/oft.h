#pragma once

struct oft_entry;

#include <stdint.h>

#include "errors.h"
#include "data-structs/vec.h"
#include "caches/inode_cache.h"
#include "dirs.h"

// Open modes stored in oft_entry::mode.
#define F_READ 0x01
#define F_WRITE 0x02
#define F_APPEND 0x03

/**
 * @brief One row in the kernel open-file table (OFT). Each active
 * k_open maps a per-process fd to a global slot holding cursor, mode,
 * refcount, and identifiers needed to reach file data on FAT or inode
 * layouts.
 */
struct oft_entry {
    /** @brief Access mode: F_READ, F_WRITE, or F_APPEND. */
    uint8_t mode;
    /** @brief Current read/write byte offset from the start of the file. */
    uint32_t cursor;
    /** @brief Reference count; decremented on k_close, row removed at 0. */
    uint16_t ref_count;
    /** @brief Path string for this open; heap-owned by this entry. */
    char *file_name;
    /** @brief Inode number */
    ino_id_t ino_id;
    /** @brief Parent directory inode id or first-block id. */
    ino_id_t parent_id;
    /** @brief Cached inode when using inode-based storage; FAT. */
    struct cached_inode_st* inode;
};

/**
 * @brief Allocate the global OFT vector and seed entries 0, 1, 2 for
 * stdin, stdout, and stderr.
 *
 * @return SUCCESS on success, or a negative err_t on failure.
 */
err_t initialize_oft();

/**
 * @brief Destroy the global OFT and release all open-file references.
 *
 * @return SUCCESS on success.
 */
err_t empty_oft(void);

/**
 * @brief Insert or reuse an OFT row for file_name. May create the
 * underlying file if the mode implies creation.
 *
 * @param mode F_READ, F_WRITE, or F_APPEND.
 * @param file_name Path string for this open instance (copied/stored).
 * @param id_in_fs First block or inode id after path resolution.
 * @param dir_block Parent directory id for this file.
 * @return New OFT slot index (kernel fd) on success, or a negative
 * err_t; F_ONLY_ONE_WRITER if opening for write while another writer exists.
 */
int oft_open_file(int mode, const char *file_name, ino_id_t ino_id, ino_id_t dir_block);

/**
 * @brief Decrement refcount for oft_id and remove the row when refcount
 * hits zero.
 *
 * @param oft_id Index into the OFT (same as kernel fd for regular files).
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int oft_close_file(int oft_id);

/**
 * @brief Find the OFT index for a file already open.
 *
 * @param ino_id File identity once allocated. If 0, file_name and parent_id
 * identify the still-unallocated dirent.
 * @param file_name File name used with parent_id when ino_id is 0.
 * @param parent_id Parent directory id used with file_name when ino_id is 0.
 * @param mode Requested open mode; F_WRITE fails if a writer exists.
 * @param oft_id Output: OFT index when found.
 * @return SUCCESS (0) on success, or a negative err_t if not found or
 * writer conflict.
 */
int find_file_in_table(ino_id_t ino_id, const char *file_name, ino_id_t parent_id, int mode, int *oft_id);

/**
 * @brief Resolve a kernel fd to its oft_entry pointer.
 *
 * @param fd Kernel file descriptor (OFT index).
 * @param entry_res Output: pointer to the live oft_entry (not a copy).
 * @return SUCCESS on success, or a negative err_t if fd is invalid.
 */
err_t get_oft_entry_by_fd(int fd, struct oft_entry** entry_res);

/**
 * @brief Increment refcount on the OFT entry for fd (fork / dup path).
 *
 * @param fd Kernel file descriptor.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int oft_add_reference(int fd);
