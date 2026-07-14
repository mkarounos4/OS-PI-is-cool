#include "dirs.h"
#include "devices.h"
#include "procfs.h"

err_t add_dirent(const char* name, ino_id_t ino_id, ino_id_t curr_dir) {
    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    block_no_t curr_block_no = get_first_block(curr_dir);
    int index = 0;
    int err;

    struct fs_dirent new_dirent = (struct fs_dirent){
        .ino_id = ino_id,
    };
    strcpy(new_dirent.name, name);

    while (curr_block_no != 0) {
        err = read_block(dir, curr_block_no);
        if (err != 0) {
            kfree(dir);
            return FILE_READ_ERROR;
        }

        // Traverse root until we reach right before the end dirent. Then, add the new dirent and create a new block if necessary.
        int i = 0;
        while (i < (int) (get_bytes_per_block() / sizeof(struct fs_dirent))) {
            if (!strcmp(dir[i].name, "\0")) {
                dir[i] = new_dirent;
                err = write_block(dir, curr_block_no);
                if (err) {
                    kfree(dir);
                    return err;
                }
                if (i + 1 < (int) (get_bytes_per_block() / sizeof(struct fs_dirent))) {
                    dir[i+1] = (struct fs_dirent){.name = "\0"};
                    err = write_block(dir, curr_block_no);
                    if (err) {
                        kfree(dir);
                        return err;
                    }
                }
                kfree(dir);
                return SUCCESS;
            }
            i++;
        }
        curr_block_no = get_ith_block_of_file_by_id(curr_dir, ++index);
    }

    // Alloc new dirent block if we've traversed to the end of dir blocks
    block_no_t new_block_no;
    if ((err = allocate_new_block_for_file_from_id(curr_dir, &new_block_no)) != SUCCESS) {
        kfree(dir);
        return err;
    }
    struct fs_dirent *new_dir = kmalloc(get_bytes_per_block());
    new_dir[0] = new_dirent;
    new_dir[1] = (struct fs_dirent){.name = "\0"};

    err = write_block(new_dir, new_block_no);
    if (err) {
        kfree(dir);
        kfree(new_dir);
        return err;
    }

    kfree(dir);
    kfree(new_dir);

    return SUCCESS;
}

err_t add_dirent_by_path(char *f_path, int file_type, int perm) {
    if (f_path == 0 || f_path[0] == 0) {
        return INVALID_FILE_NAME;
    }

    char *f_path_mut = kmalloc(sizeof(char) * (strlen(f_path) + 1));
    char *f_path_mut_root = f_path_mut;
    strcpy(f_path_mut, f_path);
    f_path_mut[strlen(f_path)] = '\0';

    ino_id_t start_dir = get_curr_dir();
    if (f_path_mut[0] == '/') {
        start_dir = 1;
        f_path_mut++;
    }

    char *token;
    token = strtok(f_path_mut, "/");
    char *next_token = strtok(NULL, "/");

    struct fs_dirent dirent;
    while (next_token != NULL) {
        err_t err = get_dirent_by_f_name(token, 1, &dirent, start_dir);
        if (err) {
            kfree(f_path_mut_root);
            return FILE_NOT_FOUND;
        }
        start_dir = dirent.ino_id;
        token = next_token;
        next_token = strtok(NULL, "/");
    }

    block_no_t block;
    struct file_operations *fops = file_type == DIRECTORY_TYPE ?
        get_default_dir_fops() : get_default_fops();
    err_t err = add_new_file_with_id(&block, file_type, perm, fops);
    if (err) {
        kfree(f_path_mut_root);
        return err;
    }
    err = add_dirent(token, block, start_dir);
    if (err) {
        kfree(f_path_mut_root);
        return err;
    }

    if (file_type == DIRECTORY_TYPE) {
        err = add_dirent(".", block, block);
        if (err) {
            kfree(f_path_mut_root);
            return err;
        }
        err = add_dirent("..", start_dir, block);
        if (err) {
            kfree(f_path_mut_root);
            return err;
        }
    }
    
    kfree(f_path_mut_root);
    return SUCCESS;
}


err_t get_dirent_by_path(const char* f_path, struct fs_dirent* dirent, int is_dir_type, ino_id_t *parent_dir, char **actual_name) {
    if (f_path == 0 || f_path[0] == 0) {
        return INVALID_FILE_NAME;
    }
    char *path_cpy = kmalloc((strlen(f_path) + 1) * sizeof(char));
    strcpy(path_cpy, f_path);
    path_cpy[strlen(f_path)] = '\0';

    ino_id_t start_dir = get_curr_dir();
    if (f_path[0] == '/') {
        start_dir = 1;
        f_path++;
    }

    char *token;
    token = strtok(path_cpy, "/");
    char *next_token = strtok(NULL, "/");

    while (token != NULL) {
        if (parent_dir != NULL) {
            *parent_dir = start_dir;
        }

        err_t err = get_dirent_by_f_name(token, next_token == NULL ? is_dir_type : 1, dirent, start_dir);

        if (err) {
            if (next_token == NULL) {
                if (actual_name != NULL) {
                    *actual_name = kmalloc(sizeof(char) * (1 + strlen(token)));
                    strcpy(*actual_name, token);
                    (*actual_name)[strlen(token)] = '\0';
                }
                if (parent_dir != NULL) {
                    *parent_dir = start_dir;
                }

                kfree(path_cpy);
                return FILE_NOT_CREATED;
            }
            kfree(path_cpy);
            return FILE_NOT_FOUND;
        }
        start_dir = dirent->ino_id;

        if (next_token == NULL) {
            next_token = token;
            token = NULL;
        } else {
            token = next_token;
            next_token = strtok(NULL, "/");
        }
    }

    if (next_token != NULL && actual_name != NULL) {
        *actual_name = kmalloc(sizeof(char) * (1 + strlen(next_token)));
        strcpy(*actual_name, next_token);
        (*actual_name)[strlen(next_token)] = '\0';
    }

    kfree(path_cpy);
    return SUCCESS;
}

int dir_lookup(const char* f_name, uint8_t is_dir_type,
               struct fs_dirent* dirent, int curr_dir) {
    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    if (dir == NULL) {
        return NO_FREE_BLOCKS;
    }

    block_no_t curr_block_no = get_first_block(curr_dir);
    int index = 0;
    while (curr_block_no != 0) {
        int err = read_block(dir, curr_block_no);
        if (err != 0) {
            kfree(dir);
            return err;
        }

        int i = 0;
        while (i < (int) (get_bytes_per_block() / sizeof(struct fs_dirent))) {
            if (!strcmp(dir[i].name, "\0")) {
                kfree(dir);
                return FILE_NOT_FOUND;
            }
            
            attributes_t metadata;
            err = get_inode_metadata(dir[i].ino_id, &metadata);
            if (err != SUCCESS) {
                kfree(dir);
                return err;
            }

            if (!strcmp(dir[i].name, f_name) && (((metadata.type == DIRECTORY_TYPE) && is_dir_type) || ((metadata.type != DIRECTORY_TYPE) && !is_dir_type))) {
                if (dirent != NULL) {
                    *dirent = dir[i];
                }
                kfree(dir);
                return SUCCESS;
            }
            i++;
        } 
        curr_block_no = get_ith_block_of_file_by_id(curr_dir, ++index);
    }
    kfree(dir);
    return FILE_NOT_FOUND;
}

err_t get_dirent_by_f_name(const char* f_name, uint8_t is_dir_type, struct fs_dirent* dirent, int curr_dir) {
    attributes_t metadata;
    err_t err = get_inode_metadata(curr_dir, &metadata);
    if (err != SUCCESS) {
        return err;
    }
    if (metadata.type != DIRECTORY_TYPE) {
        return INVALID_ARGS;
    }

    metadata.fops = get_default_dir_fops();
    err = set_inode_metadata(curr_dir, &metadata);
    if (err != SUCCESS) {
        return err;
    }

    if (metadata.fops->lookup == NULL) {
        return INVALID_ARGS;
    }

    return metadata.fops->lookup(f_name, is_dir_type, dirent, curr_dir);
}

void format_chmod_str(int perm, char res[4]) {
    res[0] = (perm & 0x1) ? 'x' : '-';
    res[1] = (perm & 0x4) ? 'r' : '-';
    res[2] = (perm & 0x2) ? 'w' : '-';
    res[3] = '\0';
}

err_t opendir(ino_id_t ino) {
    attributes_t metadata;
    err_t err = get_inode_metadata(ino, &metadata);
    if (err != SUCCESS) {
        return err;
    }
    if (metadata.type != DIRECTORY_TYPE) {
        return INVALID_ARGS;
    }

    if (metadata.i_dir == NULL) {
        metadata.i_dir = kmalloc(sizeof(struct dir_st));
        if (metadata.i_dir == NULL) {
            return NO_FREE_BLOCKS;
        }
    }

    metadata.i_dir->offset = 0;
    return set_inode_metadata(ino, &metadata);
}

int dir_open(struct oft_entry *entry) {
    if (entry == NULL) {
        return INVALID_ARGS;
    }

    return opendir(entry->ino_id);
}

err_t closedir(ino_id_t ino) {
    attributes_t metadata;
    err_t err = get_inode_metadata(ino, &metadata);
    if (err != SUCCESS) {
        return err;
    }

    if (metadata.i_dir != NULL) {
        kfree(metadata.i_dir);
        metadata.i_dir = NULL;
    }

    return set_inode_metadata(ino, &metadata);
}

int dir_close(struct oft_entry *entry) {
    if (entry == NULL) {
        return INVALID_ARGS;
    }

    return closedir(entry->ino_id);
}

err_t readdir(ino_id_t ino, fs_dirent *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }

    attributes_t metadata;
    err_t err = get_inode_metadata(ino, &metadata);
    if (err != SUCCESS) {
        return err;
    }
    if (metadata.type != DIRECTORY_TYPE) {
        return INVALID_ARGS;
    }
    if (metadata.i_dir == NULL) {
        err = opendir(ino);
        if (err != SUCCESS) {
            return err;
        }
        err = get_inode_metadata(ino, &metadata);
        if (err != SUCCESS) {
            return err;
        }
    }

    int entries_per_block = get_bytes_per_block() / (int)sizeof(struct fs_dirent);
    if (entries_per_block <= 0) {
        return INVALID_ARGS;
    }

    uint32_t offset = metadata.i_dir->offset;
    unsigned int block_index = offset / (uint32_t)entries_per_block;
    int entry_index = (int)(offset % (uint32_t)entries_per_block);
    block_no_t block_no = get_ith_block_of_file_by_id(ino, block_index);
    if (block_no == 0) {
        return FILE_NOT_FOUND;
    }

    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    if (dir == NULL) {
        return NO_FREE_BLOCKS;
    }

    err = read_block(dir, block_no);
    if (err != SUCCESS) {
        kfree(dir);
        return err;
    }

    if (!strcmp(dir[entry_index].name, "\0")) {
        kfree(dir);
        return FILE_NOT_FOUND;
    }

    *out = dir[entry_index];
    kfree(dir);

    metadata.i_dir->offset++;
    return set_inode_metadata(ino, &metadata);
}

int dir_readdir(struct oft_entry *dir, struct fs_dirent *out) {
    if (dir == NULL) {
        return INVALID_ARGS;
    }

    return readdir(dir->ino_id, out);
}

err_t list_dirents(ino_id_t ino_id, int out_fd) {
    struct cached_inode_st *inode;
    int procfs_inode = procfs_is_virtual_inode(ino_id);
    if (procfs_inode) {
        err_t err = procfs_alloc_cached_inode(ino_id, &inode);
        if (err != SUCCESS) {
            return err;
        }
    } else {
        inode = get_inode_from_cache(ino_id);
        if (inode == NULL) {
            return FILE_READ_ERROR;
        }
    }

    if (inode->inode.metadata.type != DIRECTORY_TYPE) {
        if (procfs_inode) {
            procfs_free_cached_inode(inode);
        } else {
            remove_ref_from_cache(ino_id);
        }
        return INVALID_ARGS;
    }

    inode->inode.metadata.fops = get_default_dir_fops();
    inode->dirty = 1;

    struct file_operations *fops = inode->inode.metadata.fops;
    if (fops == NULL || fops->open == NULL || fops->close == NULL ||
        fops->readdir == NULL) {
        if (procfs_inode) {
            procfs_free_cached_inode(inode);
        } else {
            remove_ref_from_cache(ino_id);
        }
        return INVALID_ARGS;
    }

    struct oft_entry dir_entry = {
        .mode = O_RDONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = ino_id,
        .inode = inode,
    };

    err_t err = fops->open(&dir_entry);
    if (err != SUCCESS) {
        if (procfs_inode) {
            procfs_free_cached_inode(inode);
        } else {
            remove_ref_from_cache(ino_id);
        }
        return err;
    }

    fs_dirent dirent;
    while ((err = fops->readdir(&dir_entry, &dirent)) == SUCCESS) {
        attributes_t metadata;
        if (procfs_is_virtual_inode(dirent.ino_id)) {
            err = procfs_get_metadata(dirent.ino_id, &metadata);
        } else {
            err = get_inode_metadata(dirent.ino_id, &metadata);
        }
        if (err != SUCCESS) {
            fops->close(&dir_entry);
            if (procfs_inode) {
                procfs_free_cached_inode(inode);
            } else {
                remove_ref_from_cache(ino_id);
            }
            return err;
        }

        char perm_str[4];
        format_chmod_str(metadata.perm, perm_str);
        int size = dirent.ino_id == 0 || procfs_is_virtual_inode(dirent.ino_id)
                   ? (int)metadata.i_size
                   : get_file_size_by_id(dirent.ino_id);
        err = fprintf(out_fd, "%u %s %u tick=%u %s\n",
                      dirent.ino_id,
                      perm_str,
                      size,
                      (unsigned int)metadata.mtime,
                      dirent.name);
        if (err < 0) {
            fops->close(&dir_entry);
            if (procfs_inode) {
                procfs_free_cached_inode(inode);
            } else {
                remove_ref_from_cache(ino_id);
            }
            return err;
        }
    }

    err_t close_err = fops->close(&dir_entry);
    if (procfs_inode) {
        procfs_free_cached_inode(inode);
    } else {
        remove_ref_from_cache(ino_id);
    }
    if (err != FILE_NOT_FOUND) {
        return err;
    }
    return close_err;
}

err_t remove_dirent_by_f_name_and_type(const char* f_name, uint8_t is_dir_type, ino_id_t parent_dir) {
    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    struct fs_dirent *next_dir = kmalloc(get_bytes_per_block());

    block_no_t curr_block_no = get_first_block(parent_dir);
    int index = 0;
    int found_dirent = 0;
    while (curr_block_no != 0) {
        int err = read_block(dir, curr_block_no);
        if (err != 0) {
            kfree(dir);
            kfree(next_dir);
            return err;
        }

        int i = 0;

        while (i < (int) (get_bytes_per_block() / sizeof(struct fs_dirent))) {
            if (!strcmp(dir[i].name, "\0")) {
                err = write_block(dir, curr_block_no);
                if (err != SUCCESS) {
                    return err;
                }

                // If we'd end up with just the end dirent
                if (found_dirent && i == 1) {
                    err = remove_last_block(parent_dir);
                }

                kfree(dir);
                kfree(next_dir);
                return err;
            }
            attributes_t metadata;
            err = get_inode_metadata(dir[i].ino_id, &metadata);
            if (err != SUCCESS) {
                kfree(dir);
                kfree(next_dir);
                return err;
            }
            if (!strcmp(dir[i].name, f_name) && (((metadata.type == DIRECTORY_TYPE) && is_dir_type) || ((metadata.type != DIRECTORY_TYPE) && !is_dir_type))) {
                found_dirent = 1;
            }
            if (found_dirent) {
                if (i == (int) (get_bytes_per_block() / sizeof(struct fs_dirent)) - 1) {
                    // This should always exist, since the last dirent of the block isn't "\0".
                    // Gets first dirent of next block to replace last block of this dirent.
                    block_no_t next_block_no = get_ith_block_of_file_by_id(parent_dir, index+1);
                    if (next_block_no == 0) {
                        kfree(dir);
                        kfree(next_dir);
                        return FILE_NOT_FOUND;
                    }
                    int err = read_block(next_dir, next_block_no);
                    if (err != 0) {
                        kfree(dir);
                        kfree(next_dir);
                        return err;
                    }
                    dir[i] = next_dir[0];
                } else {
                    dir[i] = dir[i+1];
                }
            } 
            i++;
        }
        if (found_dirent) {
            err = write_block(dir, curr_block_no);
            if (err) return err;
        }
        curr_block_no = get_ith_block_of_file_by_id(parent_dir, ++index);
    }
    kfree(dir);
    kfree(next_dir);
    return FILE_NOT_FOUND;
}
