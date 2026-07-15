#include "virtual_fs.h"

#include "fs/caches/inode_cache.h"
#include "string.h"

#define VFS_MAX_ROOT_MOUNTS 8

struct virtual_root_mount {
    char name[32];
    ino_id_t root_ino;
    const struct virtual_fs_ops *ops;
};

static struct virtual_root_mount root_mounts[VFS_MAX_ROOT_MOUNTS];
static int root_mount_count;

err_t vfs_register_root_mount(const char *name, ino_id_t root_ino,
                              const struct virtual_fs_ops *ops) {
    if (name == NULL || name[0] == '\0' || root_ino == 0 || ops == NULL) {
        return INVALID_ARGS;
    }

    for (int i = 0; i < root_mount_count; i++) {
        if (strcmp(root_mounts[i].name, name) == 0) {
            root_mounts[i].root_ino = root_ino;
            root_mounts[i].ops = ops;
            return SUCCESS;
        }
    }

    if (root_mount_count >= VFS_MAX_ROOT_MOUNTS) {
        return NO_FREE_BLOCKS;
    }

    struct virtual_root_mount *mount = &root_mounts[root_mount_count++];
    memset(mount, 0, sizeof(*mount));
    strcpy(mount->name, name);
    mount->root_ino = root_ino;
    mount->ops = ops;
    return SUCCESS;
}

int vfs_lookup_root_mount(const char *name, uint8_t is_dir_type,
                          struct fs_dirent *dirent) {
    if (name == NULL || !is_dir_type) {
        return FILE_NOT_FOUND;
    }

    for (int i = 0; i < root_mount_count; i++) {
        if (strcmp(root_mounts[i].name, name) != 0) {
            continue;
        }

        if (dirent != NULL) {
            memset(dirent, 0, sizeof(*dirent));
            strcpy(dirent->name, root_mounts[i].name);
            dirent->ino_id = root_mounts[i].root_ino;
        }
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

int vfs_root_mount_readdir(uint32_t offset, struct fs_dirent *dirent) {
    if (dirent == NULL || offset >= (uint32_t)root_mount_count) {
        return FILE_NOT_FOUND;
    }

    memset(dirent, 0, sizeof(*dirent));
    strcpy(dirent->name, root_mounts[offset].name);
    dirent->ino_id = root_mounts[offset].root_ino;
    return SUCCESS;
}

static const struct virtual_fs_ops *vfs_ops_for_inode(ino_id_t ino) {
    for (int i = 0; i < root_mount_count; i++) {
        const struct virtual_fs_ops *ops = root_mounts[i].ops;
        if (ops != NULL && ops->is_inode != NULL && ops->is_inode(ino)) {
            return ops;
        }
    }
    return NULL;
}

err_t vfs_get_inode(ino_id_t ino, struct cached_inode_st **node) {
    if (node == NULL || ino == 0) {
        return INVALID_ARGS;
    }

    const struct virtual_fs_ops *ops = vfs_ops_for_inode(ino);
    if (ops != NULL) {
        if (ops->alloc_cached_inode == NULL) {
            return INVALID_ARGS;
        }
        return ops->alloc_cached_inode(ino, node);
    }

    *node = get_inode_from_cache(ino);
    return *node == NULL ? FILE_READ_ERROR : SUCCESS;
}

void vfs_put_inode(struct cached_inode_st *node) {
    if (node == NULL) {
        return;
    }

    const struct virtual_fs_ops *ops = vfs_ops_for_inode(node->id);
    if (ops != NULL) {
        if (ops->free_cached_inode != NULL) {
            ops->free_cached_inode(node);
        }
        return;
    }

    remove_ref_from_cache(node->id);
}

err_t vfs_get_metadata(ino_id_t ino, attributes_t *metadata) {
    if (metadata == NULL || ino == 0) {
        return INVALID_ARGS;
    }

    const struct virtual_fs_ops *ops = vfs_ops_for_inode(ino);
    if (ops != NULL) {
        if (ops->get_metadata == NULL) {
            return INVALID_ARGS;
        }
        return ops->get_metadata(ino, metadata);
    }

    struct cached_inode_st *node;
    err_t err = vfs_get_inode(ino, &node);
    if (err != SUCCESS) {
        return err;
    }

    *metadata = node->inode.metadata;
    vfs_put_inode(node);
    return SUCCESS;
}

err_t vfs_format_path(ino_id_t ino, char *path, size_t size) {
    if (path == NULL || size == 0 || ino == 0) {
        return INVALID_ARGS;
    }

    const struct virtual_fs_ops *ops = vfs_ops_for_inode(ino);
    if (ops == NULL || ops->format_path == NULL) {
        return FILE_NOT_FOUND;
    }
    return ops->format_path(ino, path, size);
}
