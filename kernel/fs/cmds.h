#pragma once

#include <errno.h>

#include "errors.h"

/**
 * @brief Create each file in a NULL-terminated list if it does not yet
 * exist, or update its modification timestamp to the current time if
 * it does. Stops and returns the first error encountered.
 *
 * @param file_paths NULL-terminated array of file path strings to touch.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * or an error code propagated from k_open / k_close / k_update_file_time.
 */
err_t touch(char **file_paths);

/**
 * @brief Rename (move) a file from src_path to dest_path. If the paths
 * are identical, the file's modification time is updated instead. If a
 * file already exists at dest_path, it is unlinked first.
 *
 * @param src_path Path to the existing source file.
 * @param dest_path Path of the destination file name.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * FILE_NOT_FOUND if src_path does not exist, or an error code propagated
 * from k_unlink / k_mv_file / k_update_file_time.
 */
err_t mv(char *src_path, char *dest_path);

/**
 * @brief Remove each file in a NULL-terminated list. Stops and returns
 * the first error encountered.
 *
 * @param file_paths NULL-terminated array of file path strings to remove.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * or an error code propagated from k_unlink.
 */
err_t rm(char **file_paths);

/**
 * @brief Concatenate one or more input files (or read from stdin when
 * no input files are provided) and write the result to output_file or,
 * if output_file is NULL, to stdout.
 *
 * @param file NULL-terminated array of input file paths, or NULL/empty
 * to read from stdin.
 * @param output_file Path to the output file, or NULL to write to stdout.
 * @param flag Open mode for the output file (e.g. F_WRITE, F_APPEND);
 * ignored when output_file is NULL.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * FILE_NOT_FOUND if an input file does not exist, FILE_WRITE_ERROR if a
 * short write occurs, or an error code propagated from k_open.
 */
err_t cat(char **file, char *output_file, int flag);

/**
 * @brief Copy a file from src_path to dest_path. The flag selects
 * whether the source and/or destination lives on the filesystem
 * or on the host OS: 0 = PennFAT to PennFAT, 1 = host to PennFAT,
 * 2 = PennFAT to host.
 *
 * @param src_path Path to the source file.
 * @param dest_path Path to the destination file (created/truncated).
 * @param flag Host/PennFAT selector for source and destination
 * (0 = both PennFAT, 1 = host source, 2 = host destination).
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * FILE_NOT_FOUND if src_path does not exist on PennFAT, or an error code
 * propagated from k_open / k_read / k_write.
 */
err_t cp(char *src_path, char *dest_path, int flag);

/**
 * @brief Change the permission bits of a file. The new permissions may
 * be specified either as an octal digit (0-7) or as a string of the
 * characters 'r', 'w', and 'x'. The flag controls whether the bits are
 * set, added, or removed (interpretation defined by k_chmod).
 *
 * @param file_name Path to the file whose permissions will be modified.
 * @param new_perms Permission specifier, either an octal digit string
 * or a subset of "rwx".
 * @param flag Mode selector passed through to k_chmod (set / add / remove).
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * FILE_NOT_FOUND if file_name does not exist, INVALID_ARGS if new_perms
 * is malformed, or an error code propagated from k_chmod.
 */
err_t chmod(char *file_name, char *new_perms, int flag);

/**
 * @brief List the directory entries of dir_path and write a formatted
 * listing to the given fd. If dir_path is NULL, the current working
 * directory is listed.
 *
 * @param dir_path Path to the directory to list, or NULL for the
 * current directory.
 * @param out_fd File descriptor to write the listing to.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * or an error code propagated from k_ls.
 */
err_t ls(char *dir_path, int out_fd);

/**
 * @brief Create each directory in a NULL-terminated list. Stops and
 * returns the first error encountered.
 *
 * @param file_paths NULL-terminated array of directory path strings to
 * create.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * or an error code propagated from k_make_directory.
 */
err_t fs_mkdir(char **file_paths);

/**
 * @brief Change the current working directory to path.
 *
 * @param path Path to the directory to make current.
 * @return SUCCESS on success, FS_NOT_MOUNTED if no filesystem is mounted,
 * or an error code propagated from k_change_directory.
 */
err_t cd(char *path);
