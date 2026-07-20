#pragma once

#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "types.h"

#define DEVFS_INO_BASE UINT32_C(0xE0000000)

typedef struct attributes_t_struct attributes_t;
struct cached_inode_st;

#ifndef DEV_T_TYPE
#define DEV_T_TYPE
struct dev_st {
    uint16_t major;
    uint16_t minor;
};
#endif

err_t devfs_init(void);
int devfs_is_virtual_inode(ino_id_t ino);
err_t devfs_get_metadata(ino_id_t ino, attributes_t *metadata);
err_t devfs_alloc_cached_inode(ino_id_t ino, struct cached_inode_st **node);
void devfs_free_cached_inode(struct cached_inode_st *node);
err_t devfs_format_path(ino_id_t ino, char *path, size_t size);
int devfs_create_char_device(struct dev_st rdev);
