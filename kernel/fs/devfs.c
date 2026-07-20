#include "devfs.h"

#include "devices/devices.h"
#include "disk.h"
#include "dirs.h"
#include "fs/caches/inode_cache.h"
#include "inodes.h"
#include "memory/kmalloc.h"
#include "string.h"
#include "timer/timer.h"
#include "uart/uart.h"
#include "virtual_fs.h"

#define DEVFS_MAX_NODES 64
#define DEVFS_INO_ROOT_DIR DEVFS_INO_BASE
#define DEVFS_INO_NODE_BASE (DEVFS_INO_BASE + UINT32_C(0x1000))

struct devfs_node {
    uint8_t active;
    struct dev_st rdev;
    char name[32];
};

static struct devfs_node devfs_nodes[DEVFS_MAX_NODES];

static int devfs_dir_open(struct oft_entry *entry);
static int devfs_dir_close(struct oft_entry *entry);
static int devfs_dir_lookup(const char *f_name, uint8_t is_dir_type,
                            struct fs_dirent *dirent, int curr_dir);
static int devfs_dir_readdir(struct oft_entry *dir, struct fs_dirent *out);

static struct file_operations devfs_dir_ops = {
    .open = devfs_dir_open,
    .close = devfs_dir_close,
    .read = NULL,
    .write = NULL,
    .lookup = devfs_dir_lookup,
    .readdir = devfs_dir_readdir,
    .getattr = NULL,
};

static const struct virtual_fs_ops devfs_vfs_ops = {
    .is_inode = devfs_is_virtual_inode,
    .get_metadata = devfs_get_metadata,
    .alloc_cached_inode = devfs_alloc_cached_inode,
    .free_cached_inode = devfs_free_cached_inode,
    .format_path = devfs_format_path,
};

static ino_id_t devfs_node_ino(int index) {
    return DEVFS_INO_NODE_BASE + (ino_id_t)index;
}

static int devfs_index_from_ino(ino_id_t ino) {
    if (ino < DEVFS_INO_NODE_BASE ||
        ino >= DEVFS_INO_NODE_BASE + (ino_id_t)DEVFS_MAX_NODES) {
        return -1;
    }
    return (int)(ino - DEVFS_INO_NODE_BASE);
}

static void devfs_make_name(char name[32], struct dev_st rdev) {
    struct char_driver *driver = get_char_device(rdev.major);
    if (driver == NULL || driver->name == NULL) {
        name[0] = '\0';
        return;
    }

    int ret = snprintf(name, 32, "%s%u", driver->name,
                       (unsigned int)rdev.minor);
    if (ret >= 32) {
        name[31] = '\0';
    }
}

static int devfs_emit_dirent(struct fs_dirent *out, const char *name,
                             ino_id_t ino) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    memset(out, 0, sizeof(*out));
    strcpy(out->name, name);
    out->ino_id = ino;
    return SUCCESS;
}

static int devfs_find_node_by_name(const char *name) {
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (!devfs_nodes[i].active) {
            continue;
        }
        if (strcmp(devfs_nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int devfs_find_node_by_rdev(struct dev_st rdev) {
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (!devfs_nodes[i].active) {
            continue;
        }
        if (devfs_nodes[i].rdev.major == rdev.major &&
            devfs_nodes[i].rdev.minor == rdev.minor) {
            return i;
        }
    }
    return -1;
}

static int devfs_find_free_node(void) {
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (!devfs_nodes[i].active) {
            return i;
        }
    }
    return -1;
}

err_t devfs_init(void) {
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        memset(&devfs_nodes[i], 0, sizeof(devfs_nodes[i]));
    }

    return vfs_register_root_mount("dev", DEVFS_INO_ROOT_DIR, &devfs_vfs_ops);
}

int devfs_is_virtual_inode(ino_id_t ino) {
    if (ino == DEVFS_INO_ROOT_DIR) {
        return 1;
    }
    return devfs_index_from_ino(ino) >= 0;
}

err_t devfs_get_metadata(ino_id_t ino, attributes_t *metadata) {
    if (metadata == NULL || !devfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    memset(metadata, 0, sizeof(*metadata));
    metadata->i_links_count = 1;
    metadata->mtime = timer_get_ticks();

    if (ino == DEVFS_INO_ROOT_DIR) {
        metadata->type = DIRECTORY_TYPE;
        metadata->perm = 0x5;
        metadata->fops = &devfs_dir_ops;
        return SUCCESS;
    }

    int index = devfs_index_from_ino(ino);
    if (index < 0 || !devfs_nodes[index].active) {
        return FILE_NOT_FOUND;
    }

    struct char_driver *driver = get_char_device(devfs_nodes[index].rdev.major);
    if (driver == NULL || driver->fops == NULL) {
        return FILE_NOT_FOUND;
    }

    metadata->type = CHAR_DRIVER_TYPE;
    metadata->perm = 0x7;
    metadata->fops = (struct file_operations *)driver->fops;
    metadata->i_rdev = devfs_nodes[index].rdev;
    return SUCCESS;
}

err_t devfs_alloc_cached_inode(ino_id_t ino, struct cached_inode_st **node) {
    if (node == NULL || !devfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *cached = kmalloc(sizeof(*cached));
    if (cached == NULL) {
        return NO_FREE_BLOCKS;
    }
    memset(cached, 0, sizeof(*cached));

    err_t err = devfs_get_metadata(ino, &cached->inode.metadata);
    if (err != SUCCESS) {
        kfree(cached);
        return err;
    }

    cached->id = ino;
    cached->dirty = 0;
    *node = cached;
    return SUCCESS;
}

void devfs_free_cached_inode(struct cached_inode_st *node) {
    if (node != NULL) {
        kfree(node);
    }
}

err_t devfs_format_path(ino_id_t ino, char *path, size_t size) {
    if (path == NULL || size == 0 || !devfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    if (ino == DEVFS_INO_ROOT_DIR) {
        if (size < 5) {
            return INVALID_ARGS;
        }
        strcpy(path, "/dev");
        return SUCCESS;
    }

    int index = devfs_index_from_ino(ino);
    if (index < 0 || !devfs_nodes[index].active) {
        return FILE_NOT_FOUND;
    }

    if (snprintf(path, size, "/dev/%s", devfs_nodes[index].name) >=
        (int)size) {
        return INVALID_ARGS;
    }
    return SUCCESS;
}

int devfs_create_char_device(struct dev_st rdev) {
    struct char_driver *driver = get_char_device(rdev.major);
    if (driver == NULL || driver->fops == NULL) {
        return FILE_NOT_FOUND;
    }

    char name[32];
    devfs_make_name(name, rdev);
    if (name[0] == '\0') {
        return INVALID_ARGS;
    }

    int index = devfs_find_node_by_rdev(rdev);
    if (index < 0) {
        index = devfs_find_node_by_name(name);
    }
    if (index < 0) {
        index = devfs_find_free_node();
    }
    if (index < 0) {
        return NO_FREE_BLOCKS;
    }

    devfs_nodes[index].active = 1;
    devfs_nodes[index].rdev = rdev;
    strcpy(devfs_nodes[index].name, name);
    return SUCCESS;
}

static int devfs_dir_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL ||
        entry->ino_id != DEVFS_INO_ROOT_DIR) {
        return INVALID_ARGS;
    }
    entry->cursor = 0;
    return SUCCESS;
}

static int devfs_dir_close(struct oft_entry *entry) {
    (void)entry;
    return SUCCESS;
}

static int devfs_dir_lookup(const char *f_name, uint8_t is_dir_type,
                            struct fs_dirent *dirent, int curr_dir) {
    if (curr_dir != DEVFS_INO_ROOT_DIR || f_name == NULL) {
        return INVALID_ARGS;
    }

    if (is_dir_type && strcmp(f_name, ".") == 0) {
        return devfs_emit_dirent(dirent, ".", DEVFS_INO_ROOT_DIR);
    }
    if (is_dir_type && strcmp(f_name, "..") == 0) {
        return devfs_emit_dirent(dirent, "..", ROOT_INO);
    }
    if (is_dir_type) {
        return FILE_NOT_FOUND;
    }

    int index = devfs_find_node_by_name(f_name);
    if (index < 0) {
        return FILE_NOT_FOUND;
    }
    return devfs_emit_dirent(dirent, devfs_nodes[index].name,
                             devfs_node_ino(index));
}

static int devfs_dir_readdir(struct oft_entry *dir, struct fs_dirent *out) {
    if (dir == NULL || dir->inode == NULL || out == NULL) {
        return INVALID_ARGS;
    }

    if (dir->cursor == 0) {
        dir->cursor++;
        return devfs_emit_dirent(out, ".", DEVFS_INO_ROOT_DIR);
    }
    if (dir->cursor == 1) {
        dir->cursor++;
        return devfs_emit_dirent(out, "..", ROOT_INO);
    }

    for (int i = (int)dir->cursor - 2; i < DEVFS_MAX_NODES; i++) {
        if (!devfs_nodes[i].active) {
            continue;
        }
        dir->cursor = (uint32_t)i + 3;
        return devfs_emit_dirent(out, devfs_nodes[i].name,
                                 devfs_node_ino(i));
    }

    return FILE_NOT_FOUND;
}
