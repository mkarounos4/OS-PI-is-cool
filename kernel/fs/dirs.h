#pragma once

#include <math.h>
#include <time.h>
#include "disk.h"
#include "kapi.h"

// Flag bits for update_dirent_by_f_name, indicating which of the
// optional parameters should actually overwrite the existing dirent.
#define EDIT_TYPE 0x02
#define EDIT_PERM 0x04
#define EDIT_FNAME 0x08
#define EDIT_ID 0x20
// When set together with EDIT_PERM, the permission bits are AND'd into
// the existing perm field rather than replacing it (used to clear bits).
#define AND_PERM 0x10

// File type values stored in the dirent `type` field.
#define UNKNOWN_F_TYPE 0x0
#define REGULAR_F_TYPE 0x1
#define DIRECTORY_F_TYPE 0x2
#define SYMBOLIC_F_TYPE 0x4

/**
 * @brief Create a new directory entry with the given parameters and
 * append it to the directory identified by curr_dir.
 *
 * @param name Null-terminated file name for the new dirent
 * (truncated to 32 chars if longer).
 * @param firstBlock First block of the file's data (FAT) or inode id
 * (inode filesystems).
 * @param type File type (one of the *_F_TYPE constants).
 * @param perm Permission bits (rwx, see inodes.h).
 * @param curr_dir Inode id / first block of the directory the entry
 * should be added to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t add_dirent(const char* name, ino_id_t ino_id, uint8_t type, uint8_t perm, ino_id_t curr_dir);

/**
 * @brief On-disk directory entry for a single file or subdirectory.
 * The layout is a fixed-size record (including trailing reserved bytes)
 * so entries pack predictably into directory blocks.
 */
struct fs_dirent {
    /** @brief NUL-padded file name (at most 31 chars + terminator). */
    char name[32];
    /** @brief inode id. */
    uint16_t ino_id;
    /** @brief Entry kind: UNKNOWN_F_TYPE, REGULAR_F_TYPE, DIRECTORY_F_TYPE, or SYMBOLIC_F_TYPE. */
    uint8_t type;
    /** @brief Permission bits (typically rwx-style low 3 bits). */
    uint8_t perm;
    /** @brief Last modification time. */
    time_t mtime;
    /** @brief Padding reserved for future fields; keep struct size stable. */
    uint8_t reserved[16];
};

/**
 * @brief Update fields of the dirent with name f_name inside the
 * parent directory parent_id. Only the fields whose corresponding
 * EDIT_* bits are set in flags are overwritten.
 *
 * @param f_name Name of the dirent to update.
 * @param parent_id Inode id / first block of the containing directory.
 * @param curr_type Current file type of the dirent (used to locate
 * the correct entry when names can alias across types).
 * @param flags Bitmask of EDIT_* flags selecting which fields to update.
 * Pass AND_PERM together with EDIT_PERM to AND the new perm into the
 * existing value instead of replacing it.
 * @param size New size (used only if EDIT_SIZE is set).
 * @param perm New permission bits (used only if EDIT_PERM is set).
 * @param new_file_type New file type (used only if EDIT_TYPE is set).
 * @param new_f_name New file name (used only if EDIT_FNAME is set).
 * @param new_id New first block / inode id (used only if EDIT_ID is set).
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t update_dirent_by_f_name(const char* f_name, ino_id_t parent_id, uint8_t curr_type, int flags, uint8_t perm, uint8_t new_file_type, const char* new_f_name, uint16_t new_id);

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

/**
 * @brief Write a formatted listing of the dirents in the directory
 * starting at firstBlock to the given file descriptor.
 *
 * @param firstBlock First block / inode id of the directory to list.
 * @param out_fd File descriptor to write the listing to.
 * @return SUCCESS on success, or a negative error code on failure.
 */
err_t list_dirents(uint16_t ino_id, int out_fd);

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
err_t remove_dirent_by_f_name_and_type(const char* f_name, uint8_t file_type, int parent_dir);

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
