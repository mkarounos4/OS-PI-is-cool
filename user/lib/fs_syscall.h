#pragma once

#include <stdint.h>

#include "syscall.h"

typedef int32_t err_t;
typedef __SIZE_TYPE__ size_t;
typedef __SIZE_TYPE__ ssize_t;

#define O_TRUNC 4
#define O_CREAT 8
#define O_APPEND 16
#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR 3

#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define STDIN_FILENO STDIN
#define STDOUT_FILENO STDOUT
#define STDERR_FILENO STDERR

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

static inline int write(int fd, const char *buf, int n) {
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

static inline err_t ls(char *dir_path) {
    return (err_t)sys_call1(S_FS_LS, (long)(uintptr_t)dir_path);
}

static inline err_t fs_mkdir(char **file_paths) {
    return (err_t)sys_call1(S_FS_MKDIR, (long)(uintptr_t)file_paths);
}

static inline err_t cd(char *path) {
    return (err_t)sys_call1(S_FS_CD, (long)(uintptr_t)path);
}

static inline char *getcwd(char *path, size_t size) {
    long err = sys_call2(S_GETCWD, (long)(uintptr_t)path, (long)size);
    return err < 0 ? (char *)(uintptr_t)err : path;
}

static inline int stat(const char *path, struct fs_stat_st *stat) {
    return (int)sys_call2(S_STAT,
                          (long)(uintptr_t)path,
                          (long)(uintptr_t)stat);
}
