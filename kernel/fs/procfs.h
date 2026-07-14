#pragma once

#include <stdint.h>

#include "errors.h"
#include "types.h"

#define PROCFS_INO_BASE UINT32_C(0xF0000000)

typedef struct attributes_t_struct attributes_t;
struct cached_inode_st;

struct proc_st {
    uint8_t kind;
    uint8_t file_id;
    int32_t pid;
    uint32_t offset;
    char *buffer;
    uint32_t size;
};

err_t procfs_init(void);
int procfs_is_virtual_inode(ino_id_t ino);
err_t procfs_get_metadata(ino_id_t ino, attributes_t *metadata);
err_t procfs_alloc_cached_inode(ino_id_t ino, struct cached_inode_st **node);
void procfs_free_cached_inode(struct cached_inode_st *node);
