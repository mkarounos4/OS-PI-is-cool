#pragma once

#include "disk.h"
#include "kapi.h"
#include "memory/kmalloc.h"
#include "uart/uart.h"

// Flag bits for update_dirent_by_f_name, indicating which of the
// optional parameters should actually overwrite the existing dirent.
#define EDIT_TYPE 0x02
#define EDIT_PERM 0x04
#define EDIT_FNAME 0x08
#define EDIT_ID 0x20
// When set together with EDIT_PERM, the permission bits are AND'd into
// the existing perm field rather than replacing it (used to clear bits).
#define AND_PERM 0x10

/**
 * @brief Create a new directory entry with the given parameters and
 * append it to the directory identified by curr_dir.
 *
 * @param name Null-terminated file name for the new dirent
 * (truncated to 32 chars if longer).
 * @param firstBlock First block of the file's data (FAT) or inode id
 * (inode filesystems).
 * @param curr_dir Inode id / first block of the directory the entry
 * should be added to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t add_dirent(const char* name, ino_id_t ino_id, ino_id_t curr_dir);

/**
 * @brief On-disk directory entry for a single file or subdirectory.
 * The layout is a fixed-size record (including trailing reserved bytes)
 * so entries pack predictably into directory blocks.
 */
struct fs_dirent {
    /** @brief NUL-padded file name (at most 31 chars + terminator). */
    char name[32];
    /** @brief inode id. */
    ino_id_t ino_id;
    /** @brief Padding reserved for future fields; keep struct size stable. */
    uint8_t reserved[26];
};
typedef struct fs_dirent fs_dirent;

struct dir_st {
    uint32_t offset;
};

/**
 * @brief Look up a dirent by name within the given directory and
 * copy it into the caller-provided struct.
 *
 * @param f_name Name of the file to find.
 * @param file_type File type to match against, or UNKNOWN_F_TYPE to
 * match any type.
 * @param dirent Output: receives a copy of the matched dirent.
 * @param curr_dir Inode id / first block of the directory to search.
 * @return SUCCESS on success, FILE_NOT_FOUND if no matching dirent
 * exists in that directory, or a negative error code on failure.
 */
err_t get_dirent_by_f_name(const char* f_name, uint8_t file_type, struct fs_dirent* dirent, int curr_dir);

/**
 * @brief Resolve a (possibly multi-component) path to its dirent,
 * optionally returning the containing directory and the final path
 * component so that callers can modify the entry in place.
 *
 * @param f_path Path to resolve (absolute or relative to cwd).
 * @param dirent Output: receives a copy of the resolved dirent.
 * @param file_type Expected file type (one of *_F_TYPE), or
 * UNKNOWN_F_TYPE to match any.
 * @param parent_dir Output (optional): receives the id of the directory
 * containing the final component. Pass NULL if not needed.
 * @param actual_name Output (optional): receives a pointer to the
 * final component of f_path. Pass NULL if not needed.
 * @return SUCCESS on success, FILE_NOT_FOUND if any component does
 * not exist, or a negative error code on failure.
 */
err_t get_dirent_by_path(const char* f_path, struct fs_dirent* dirent, int file_type, ino_id_t *parent_dir, char **actual_name);

err_t opendir(ino_id_t ino);
err_t closedir(ino_id_t ino);
err_t readdir(ino_id_t ino, fs_dirent *out);

/**
 * @brief Write a formatted listing of the dirents in the directory
 * starting at firstBlock to the given file descriptor.
 *
 * @param firstBlock First block / inode id of the directory to list.
 * @param out_fd File descriptor to write the listing to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t list_dirents(ino_id_t ino_id, int out_fd);

/**
 * @brief Remove the dirent matching f_name and file_type from the
 * given parent directory. Does not free the underlying file blocks;
 * that is the caller's responsibility (usually via k_unlink).
 *
 * @param f_name Name of the dirent to remove.
 * @param file_type File type of the entry (disambiguates when names
 * collide across types).
 * @param parent_dir Inode id / first block of the containing directory.
 * @return SUCCESS on success, FILE_NOT_FOUND if no matching entry,
 * or a negative error code on failure.
 */
err_t remove_dirent_by_f_name_and_type(const char* f_name, uint8_t file_type, ino_id_t parent_dir);

/**
 * @brief Create a new dirent at the given path, allocating any missing
 * intermediate state and wiring the entry into its parent directory.
 *
 * @param f_path Full path at which to create the dirent. All
 * intermediate directories must already exist.
 * @param file_type File type for the new entry (one of *_F_TYPE).
 * @param perm Permission bits to store on the new entry.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t add_dirent_by_path(char *f_path, int file_type, int perm);
