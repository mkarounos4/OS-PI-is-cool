#include "virtual_fs.h"

#include "fs/caches/inode_cache.h"
#include "string.h"
#include "uart/uart.h"
#include "sync/spinlock.h"

#define VFS_MAX_ROOT_MOUNTS 8

struct virtual_root_mount {
    char name[32];
    ino_id_t root_ino;
    const struct virtual_fs_ops *ops;
};

static struct virtual_root_mount root_mounts[VFS_MAX_ROOT_MOUNTS];
static int root_mount_count;
static spinlock_t vfs_mount_lock = SPINLOCK_INIT;

err_t vfs_register_root_mount(const char *name, ino_id_t root_ino,
                              const struct virtual_fs_ops *ops) {
    if (name == NULL || name[0] == '\0' || root_ino == 0 || ops == NULL) {
        return INVALID_ARGS;
    }

    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    for (int i = 0; i < root_mount_count; i++) {
        if (strcmp(root_mounts[i].name, name) == 0) {
            root_mounts[i].root_ino = root_ino;
            root_mounts[i].ops = ops;
            spin_unlock_irqrestore(&vfs_mount_lock, flags);
            return SUCCESS;
        }
    }

    if (root_mount_count >= VFS_MAX_ROOT_MOUNTS) {
        spin_unlock_irqrestore(&vfs_mount_lock, flags);
        return NO_FREE_BLOCKS;
    }

    struct virtual_root_mount *mount = &root_mounts[root_mount_count++];
    memset(mount, 0, sizeof(*mount));
    strcpy(mount->name, name);
    mount->root_ino = root_ino;
    mount->ops = ops;
    spin_unlock_irqrestore(&vfs_mount_lock, flags);
    return SUCCESS;
}

int vfs_lookup_root_mount(const char *name, struct fs_dirent *dirent) {
    if (name == NULL ) {
        return FILE_NOT_FOUND;
    }

    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    for (int i = 0; i < root_mount_count; i++) {
        if (strcmp(root_mounts[i].name, name) != 0) {
            continue;
        }

        if (dirent != NULL) {
            memset(dirent, 0, sizeof(*dirent));
            strcpy(dirent->name, root_mounts[i].name);
            dirent->ino_id = root_mounts[i].root_ino;
        }
        spin_unlock_irqrestore(&vfs_mount_lock, flags);
        return SUCCESS;
    }

    spin_unlock_irqrestore(&vfs_mount_lock, flags);
    return FILE_NOT_FOUND;
}

int vfs_root_mount_readdir(uint32_t offset, struct fs_dirent *dirent) {
    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    if (dirent == NULL || offset >= (uint32_t)root_mount_count) {
        spin_unlock_irqrestore(&vfs_mount_lock, flags);
        return FILE_NOT_FOUND;
    }

    memset(dirent, 0, sizeof(*dirent));
    strcpy(dirent->name, root_mounts[offset].name);
    dirent->ino_id = root_mounts[offset].root_ino;
    spin_unlock_irqrestore(&vfs_mount_lock, flags);
    return SUCCESS;
}

int vfs_root_mount_count(void) {
    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    int count = root_mount_count;
    spin_unlock_irqrestore(&vfs_mount_lock, flags);
    return count;
}

err_t vfs_format_mounts(char *buf, size_t size) {
    if (buf == NULL || size == 0) {
        return INVALID_ARGS;
    }

    int len = snprintf(buf, size, "PATH TYPE ROOT_INO\n");
    if (len < 0) {
        return len;
    }

    size_t used = len < (int)size ? (size_t)len : size - 1;
    int ret = snprintf(buf + used, size - used, "/ rootfs %u\n", ROOT_INO);
    if (ret < 0) {
        return ret;
    }
    len += ret;

    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    for (int i = 0; i < root_mount_count; i++) {
        used = len < (int)size ? (size_t)len : size - 1;
        ret = snprintf(buf + used, size - used, "/%s %s %u\n",
                       root_mounts[i].name,
                       root_mounts[i].name,
                       root_mounts[i].root_ino);
        if (ret < 0) {
            spin_unlock_irqrestore(&vfs_mount_lock, flags);
            return ret;
        }
        len += ret;
    }
    spin_unlock_irqrestore(&vfs_mount_lock, flags);

    return len;
}

static const struct virtual_fs_ops *vfs_ops_for_inode(ino_id_t ino) {
    uint64_t flags = spin_lock_irqsave(&vfs_mount_lock);
    for (int i = 0; i < root_mount_count; i++) {
        const struct virtual_fs_ops *ops = root_mounts[i].ops;
        if (ops != NULL && ops->is_inode != NULL && ops->is_inode(ino)) {
            spin_unlock_irqrestore(&vfs_mount_lock, flags);
            return ops;
        }
    }
    spin_unlock_irqrestore(&vfs_mount_lock, flags);
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

    uint64_t flags = cached_inode_lock(node);
    *metadata = node->inode.metadata;
    cached_inode_unlock(node, flags);
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
