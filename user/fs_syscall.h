#pragma once

#include <stdint.h>

#include "syscall.h"

typedef int32_t err_t;
typedef __SIZE_TYPE__ size_t;
typedef __SIZE_TYPE__ ssize_t;

#define F_READ 0x01
#define F_WRITE 0x02
#define F_APPEND 0x03

#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

#define O_RDONLY F_READ
#define O_WRONLY F_WRITE
#define O_RDWR F_WRITE
#define O_APPEND F_APPEND
#define O_CREAT 0x4
#define O_TRUNC 0x8

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define STDIN_FILENO STDIN
#define STDOUT_FILENO STDOUT
#define STDERR_FILENO STDERR

static inline int open(const char *fname, int mode) {
    return (int)sys_call2(S_FS_OPEN, (long)(uintptr_t)fname, mode);
}

static inline int close(int fd) {
    return (int)sys_call1(S_FS_CLOSE, fd);
}

static inline int lseek(int fd, int offset, int whence) {
    return (int)sys_call3(S_FS_LSEEK, fd, offset, whence);
}

static inline int read(int fd, char *buf, int n) {
    return (int)sys_call3(S_FS_READ, fd, (long)(uintptr_t)buf, n);
}

static inline int write(int fd, char *buf, int n) {
    return (int)sys_call3(S_FS_WRITE, fd, (long)(uintptr_t)buf, n);
}

static inline err_t touch(char **file_paths) {
    return (err_t)sys_call1(S_FS_TOUCH, (long)(uintptr_t)file_paths);
}

static inline err_t mv(char *src_path, char *dest_path) {
    return (err_t)sys_call2(S_FS_MV,
                            (long)(uintptr_t)src_path,
                            (long)(uintptr_t)dest_path);
}

static inline err_t rm(char **file_paths) {
    return (err_t)sys_call1(S_FS_RM, (long)(uintptr_t)file_paths);
}

static inline err_t cat(char **file, char *output_file, int flag) {
    return (err_t)sys_call3(S_FS_CAT,
                            (long)(uintptr_t)file,
                            (long)(uintptr_t)output_file,
                            flag);
}

static inline err_t cp(char *src_path, char *dest_path, int flag) {
    return (err_t)sys_call3(S_FS_CP,
                            (long)(uintptr_t)src_path,
                            (long)(uintptr_t)dest_path,
                            flag);
}

static inline err_t fs_chmod(char *file_name, char *new_perms, int flag) {
    return (err_t)sys_call3(S_FS_CHMOD,
                            (long)(uintptr_t)file_name,
                            (long)(uintptr_t)new_perms,
                            flag);
}

static inline err_t ls(char *dir_path, int out_fd) {
    return (err_t)sys_call2(S_FS_LS, (long)(uintptr_t)dir_path, out_fd);
}

static inline err_t fs_mkdir(char **file_paths) {
    return (err_t)sys_call1(S_FS_MKDIR, (long)(uintptr_t)file_paths);
}

static inline err_t cd(char *path) {
    return (err_t)sys_call1(S_FS_CD, (long)(uintptr_t)path);
}
