#pragma once

#include "traps/traps.h"
#include "errors.h"
#include "memory/kmalloc.h"
#include "oft.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// k_lseek whence values (same semantics as POSIX lseek).
#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

#define O_TRUNC 4
#define O_CREAT 8
#define O_APPEND 16
#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR 3

struct fs_stat_st {
    uint32_t ino_id;
    uint16_t links_count;
    uint8_t type;
    uint8_t perm;
    uint32_t size;
    uint32_t blocks;
    uint64_t mtime;
    uint16_t rdev_major;
    uint16_t rdev_minor;
};

/**
 * @brief Open a file by path, create an open-file-table entry, and
 * return a kernel file descriptor. Creates the file on disk if the
 * mode requires it and the name is valid.
 *
 * @param fname Path to the file (relative to cwd or absolute).
 * @param mode Open mode (F_READ, F_WRITE, or F_APPEND from oft.h).
 * @return Non-negative fd on success, or a negative err_t on failure
 * (e.g. FS_NOT_MOUNTED, FILE_NOT_FOUND, F_ONLY_ONE_WRITER).
 */
int k_open(const char *fname, int mode);

/**
 * @brief Close the file associated with fd: decrements OFT refcount
 * and removes the entry when it reaches zero.
 *
 * @param fd Kernel file descriptor returned by k_open.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_close(struct oft_entry *entry);

/**
 * @brief Read at most n bytes from the file at fd into buf.
 *
 * @param fd Kernel file descriptor.
 * @param buf Destination buffer.
 * @param n Maximum number of bytes to read.
 * @return Number of bytes read, 0 on EOF, or a negative err_t on error.
 */
int k_read(struct oft_entry *entry, char *buf, size_t n);

/**
 * @brief Write at most n bytes from buf into the file at fd.
 *
 * @param fd Kernel file descriptor.
 * @param buf Source buffer.
 * @param n Number of bytes to write.
 * @return Number of bytes written on success, or a negative err_t on error.
 */
int k_write(struct oft_entry *entry, const char *buf, size_t n);

/**
 * @brief Update the modification time on the dirent for file_name.
 *
 * @param file_name Path of the file whose mtime is refreshed.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_update_file_time(const char *file_name);

/**
 * @brief Reposition the read/write cursor for the file at fd.
 *
 * @param fd Kernel file descriptor.
 * @param offset Byte offset (interpretation depends on whence).
 * @param whence F_SEEK_SET, F_SEEK_CUR, or F_SEEK_END.
 * @return New cursor position on success, or a negative err_t on error.
 */
int k_lseek(int fd, int offset, int whence);

/**
 * @brief Change permission bits on the dirent for file_name. The flag
 * selects set / add / remove semantics (see chmod builtin).
 *
 * @param file_name Path of the file to chmod.
 * @param new_perms New permission bits (octal 0-7 or rwx mask).
 * @param flag Mode passed through to the underlying dirent update.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_chmod(const char *file_name, uint8_t new_perms, int flag);

/**
 * @brief Rename a file from src_path to dest_path (update dirent name
 * and parent linkage as required).
 *
 * @param src_path Existing file path.
 * @param dest_path New path / name.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_mv_file(const char *src_path, const char *dest_path);

/**
 * @brief Remove the file fname (dirent and backing storage).
 *
 * @param fname Path of the file to unlink.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_unlink(const char*fname);

/**
 * @brief List directory entries. If filename is NULL, lists the cwd.
 *
 * @param filename Directory path to list, or NULL for cwd.
 * @param out_fs File descriptor to write listing output to.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_ls(const char *filename, int out_fs);

/**
 * @brief Test whether a regular file dirent exists at f_name.
 *
 * @param f_name Path or file name to look up.
 * @return Non-zero if a FILE_TYPE dirent resolves successfully, 0 if
 * the path does not exist or get_dirent_by_path fails.
 */
int k_check_if_exists(const char *f_name);

/**
 * @brief Increment the refcount on the OFT entry for fd (e.g. when
 * duplicating the open file into a child process).
 *
 * @param fd Kernel file descriptor.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_file_add_reference(int fd);

/**
 * @brief Create a directory at f_path.
 *
 * @param f_path Path of the new directory.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_make_directory(char *f_path);

/**
 * @brief Change the current working directory to f_path.
 *
 * @param f_path Target directory path.
 * @return SUCCESS (0) on success, or a negative err_t on failure.
 */
int k_change_directory(char *f_path);

/**
 * @brief Return whether f_name refers to an executable file for the shell.
 *
 * @param f_name Path or name to check.
 * @return true if executable, false otherwise.
 */
bool k_check_if_executable(char *f_name);

int k_stat(const char *path, struct fs_stat_st *stat);

/**
 * @brief Parse a user ELF image from path and dispatch each loadable
 * segment to the user table metadata. On success, installs a fresh user
 * page table and updates the trap frame to enter the ELF.
 *
 * @param path Filesystem path to an ELF executable.
 * @param frame Current syscall trap frame to update for exec return.
 * @return SUCCESS on successful parse, or a negative err_t on failure.
 */
int k_exec(const char *path, char *const argv[], struct trap_frame *frame,
           struct trap_frame **next_frame);
int k_exec_process(int pid, const char *path, char *const argv[]);

struct file_operations *get_default_fops();
struct file_operations *get_default_dir_fops();
