#pragma once

#include <stddef.h>

#include "dirs.h"
#include "errors.h"
#include "types.h"

typedef struct attributes_t_struct attributes_t;
struct cached_inode_st;

struct virtual_fs_ops {
    int (*is_inode)(ino_id_t ino);
    err_t (*get_metadata)(ino_id_t ino, attributes_t *metadata);
    err_t (*alloc_cached_inode)(ino_id_t ino, struct cached_inode_st **node);
    void (*free_cached_inode)(struct cached_inode_st *node);
    err_t (*format_path)(ino_id_t ino, char *path, size_t size);
};

err_t vfs_register_root_mount(const char *name, ino_id_t root_ino,
                              const struct virtual_fs_ops *ops);
int vfs_lookup_root_mount(const char *name, uint8_t is_dir_type,
                          struct fs_dirent *dirent);
int vfs_root_mount_readdir(uint32_t offset, struct fs_dirent *dirent);
err_t vfs_get_inode(ino_id_t ino, struct cached_inode_st **node);
void vfs_put_inode(struct cached_inode_st *node);
err_t vfs_get_metadata(ino_id_t ino, attributes_t *metadata);
err_t vfs_format_path(ino_id_t ino, char *path, size_t size);
