#include "symlink.h"

#include "dirs.h"
#include "disk.h"
#include "errors.h"
#include "kapi.h"
#include "memory/kmalloc.h"
#include "oft.h"
#include "string.h"
#include "virtual_fs.h"

#define SYMLINK_MAX_FOLLOW_DEPTH 8

enum symlink_fop {
    SYMLINK_FOP_READ,
    SYMLINK_FOP_WRITE,
    SYMLINK_FOP_LOOKUP,
    SYMLINK_FOP_READDIR,
    SYMLINK_FOP_GETATTR,
};

struct symlink_lookup_args {
    const char *name;
    struct fs_dirent *dirent;
};

int default_read(struct oft_entry *entry, char *buf, size_t n);

static err_t symlink_read_target_path(ino_id_t ino, char **target_path) {
    if (target_path == NULL) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *inode = get_inode_from_cache(ino);
    if (inode == NULL) {
        return FILE_READ_ERROR;
    }

    int size = inode->inode.metadata.i_size;
    if (size <= 0) {
        remove_ref_from_cache(ino);
        return FILE_NOT_FOUND;
    }

    char *path = kmalloc((size_t)size + 1);
    if (path == NULL) {
        remove_ref_from_cache(ino);
        return NO_FREE_BLOCKS;
    }

    struct oft_entry entry = {
        .mode = O_RDONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = ino,
        .inode = inode,
    };
    int bytes = default_read(&entry, path, (size_t)size);
    remove_ref_from_cache(ino);
    if (bytes < 0) {
        kfree(path);
        return bytes;
    }

    path[bytes] = '\0';
    *target_path = path;
    return SUCCESS;
}

static err_t symlink_resolve_target(ino_id_t link_ino,
                                    uint8_t mode,
                                    uint32_t cursor,
                                    struct oft_entry *target,
                                    struct cached_inode_st **target_inode) {
    ino_id_t current = link_ino;

    for (int depth = 0; depth < SYMLINK_MAX_FOLLOW_DEPTH; depth++) {
        char *target_path = NULL;
        err_t err = symlink_read_target_path(current, &target_path);
        if (err != SUCCESS) {
            return err;
        }

        struct fs_dirent dirent;
        err = get_dirent_by_path(target_path, &dirent, NULL, NULL);
        kfree(target_path);
        if (err != SUCCESS) {
            return err == FILE_NOT_CREATED ? FILE_NOT_FOUND : err;
        }

        attributes_t metadata;
        err = get_inode_metadata(dirent.ino_id, &metadata);
        if (err != SUCCESS) {
            return err;
        }

        if (metadata.type == SYMLINK_TYPE) {
            current = dirent.ino_id;
            continue;
        }

        err = vfs_get_inode(dirent.ino_id, target_inode);
        if (err != SUCCESS) {
            return err;
        }

        *target = (struct oft_entry) {
            .mode = mode,
            .cursor = cursor,
            .ref_count = 1,
            .ino_id = dirent.ino_id,
            .inode = *target_inode,
        };
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

static int symlink_dispatch(struct oft_entry *entry,
                            enum symlink_fop op,
                            void *arg,
                            size_t count) {
    if (entry == NULL) {
        return INVALID_ARGS;
    }

    struct oft_entry target;
    struct cached_inode_st *target_inode = NULL;
    err_t err = symlink_resolve_target(entry->ino_id, entry->mode,
                                       entry->cursor, &target, &target_inode);
    if (err != SUCCESS) {
        return err;
    }

    struct file_operations *fops = target.inode->inode.metadata.fops;
    int ret = SUCCESS;
    switch (op) {
    case SYMLINK_FOP_READ:
        ret = fops != NULL && fops->read != NULL ?
            fops->read(&target, (char *)arg, count) : IS_A_DIRECTORY;
        break;
    case SYMLINK_FOP_WRITE:
        ret = fops != NULL && fops->write != NULL ?
            fops->write(&target, (const char *)arg, count) : IS_A_DIRECTORY;
        break;
    case SYMLINK_FOP_LOOKUP:
    {
        struct symlink_lookup_args *lookup = arg;
        ret = fops != NULL && fops->lookup != NULL ?
            fops->lookup(lookup->name, lookup->dirent, target.ino_id) :
            NOT_A_DIRECTORY;
        break;
    }
    case SYMLINK_FOP_READDIR:
        ret = fops != NULL && fops->readdir != NULL ?
            fops->readdir(&target, (struct fs_dirent *)arg) : NOT_A_DIRECTORY;
        break;
    case SYMLINK_FOP_GETATTR:
        ret = fops != NULL && fops->getattr != NULL ?
            fops->getattr(target.ino_id, (const char *)arg) : SUCCESS;
        break;
    }

    entry->cursor = target.cursor;
    vfs_put_inode(target_inode);
    return ret;
}

int symlink_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return INVALID_ARGS;
    }

    struct oft_entry target;
    struct cached_inode_st *target_inode = NULL;
    err_t err = symlink_resolve_target(entry->ino_id, entry->mode,
                                       entry->cursor, &target, &target_inode);
    if (err != SUCCESS) {
        return err;
    }

    struct file_operations *fops = target.inode->inode.metadata.fops;
    if (fops != NULL && fops->open != NULL) {
        err = fops->open(&target);
        if (err != SUCCESS) {
            vfs_put_inode(target_inode);
            return err;
        }
    }

    struct cached_inode_st *link_inode = entry->inode;
    entry->ino_id = target.ino_id;
    entry->inode = target_inode;
    entry->cursor = target.cursor;
    vfs_put_inode(link_inode);
    return SUCCESS;
}

int symlink_close(struct oft_entry *entry) {
    (void)entry;
    return SUCCESS;
}

int symlink_read(struct oft_entry *entry, char *buffer, size_t count) {
    return symlink_dispatch(entry, SYMLINK_FOP_READ, buffer, count);
}

int symlink_write(struct oft_entry *entry, const char *buffer, size_t count) {
    return symlink_dispatch(entry, SYMLINK_FOP_WRITE, (void *)buffer, count);
}

int symlink_lookup(const char *f_name, struct fs_dirent *dirent, int curr_dir) {
    struct oft_entry entry = {
        .mode = O_RDONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = (ino_id_t)curr_dir,
        .inode = NULL,
    };
    struct symlink_lookup_args args = {
        .name = f_name,
        .dirent = dirent,
    };
    return symlink_dispatch(&entry, SYMLINK_FOP_LOOKUP, &args, 0);
}

int symlink_readdir(struct oft_entry *dir, struct fs_dirent *out) {
    return symlink_dispatch(dir, SYMLINK_FOP_READDIR, out, 0);
}

int symlink_getattr(int curr_dir, const char *name) {
    struct oft_entry entry = {
        .mode = O_RDONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = (ino_id_t)curr_dir,
        .inode = NULL,
    };
    return symlink_dispatch(&entry, SYMLINK_FOP_GETATTR, (void *)name, 0);
}

int symlink_readlink(const char *path, char *buffer, size_t count) {
    if (path == NULL || buffer == NULL) {
        return INVALID_ARGS;
    }

    struct fs_dirent dirent;
    err_t err = get_dirent_by_path(path, &dirent, NULL, NULL);
    if (err != SUCCESS) {
        return err == FILE_NOT_CREATED ? FILE_NOT_FOUND : err;
    }

    attributes_t metadata;
    err = get_inode_metadata(dirent.ino_id, &metadata);
    if (err != SUCCESS) {
        return err;
    }
    if (metadata.type != SYMLINK_TYPE) {
        return INVALID_ARGS;
    }

    char *target_path = NULL;
    err = symlink_read_target_path(dirent.ino_id, &target_path);
    if (err != SUCCESS) {
        return err;
    }

    size_t len = strlen(target_path);
    size_t to_copy = MIN(len, count);
    for (size_t i = 0; i < to_copy; i++) {
        buffer[i] = target_path[i];
    }
    kfree(target_path);
    return (int)to_copy;
}

struct file_operations symlink_fops = {
    .open = symlink_open,
    .close = symlink_close,
    .read = symlink_read,
    .write = symlink_write,
    .lookup = symlink_lookup,
    .readdir = symlink_readdir,
    .getattr = symlink_getattr,
};
