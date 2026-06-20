#include "dirs.h"

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

err_t update_dirent_by_f_name(const char* f_name, ino_id_t parent_id, uint8_t curr_type, int flags, uint8_t perm, uint8_t new_file_type, const char* new_f_name, ino_id_t new_id) {
    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    block_no_t curr_block_no = get_first_block(parent_id);
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
            if (!strcmp(dir[i].name, f_name) && metadata.type == curr_type) {
                int inode_flags = INODE_EDIT_MTIME;
                if (flags & EDIT_TYPE) {
                    inode_flags |= INODE_EDIT_TYPE;
                }
                if (flags & EDIT_PERM) {
                    inode_flags |= INODE_EDIT_PERM;
                    if (flags & AND_PERM) {
                        inode_flags |= INODE_AND_PERM;
                    }
                }
                ino_id_t metadata_id = (flags & EDIT_ID) ? new_id : dir[i].ino_id;
                if (metadata_id != 0) {
                    err = update_inode_metadata(metadata_id, inode_flags, new_file_type, perm);
                    if (err != SUCCESS) {
                        kfree(dir);
                        return err;
                    }
                }
                if (flags & EDIT_FNAME) {
                    strncpy(dir[i].name, new_f_name, sizeof(dir[i].name));
                }
                if (flags & EDIT_ID) {
                    dir[i].ino_id = new_id;
                }
                err = write_block(dir, curr_block_no);
                kfree(dir);
                if (err) return err;
                return SUCCESS;
            }
            i++;
        } 
        curr_block_no = get_ith_block_of_file_by_id(parent_id, ++index);
    }
    kfree(dir);
    return FILE_NOT_FOUND;
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
    err_t err = add_new_file_with_id(&block, file_type, perm);
    if (err) {
        kfree(f_path_mut_root);
        return err;
    }
    err = add_dirent(token, block, start_dir);
    if (err) {
        kfree(f_path_mut_root);
        return err;
    }

    if (file_type == DIRECTORY_F_TYPE) {
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

err_t get_dirent_by_f_name(const char* f_name, uint8_t is_dir_type, struct fs_dirent* dirent, int curr_dir) {
    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
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
    return FILE_NOT_FOUND;
}

void format_chmod_str(int perm, char res[4]) {
    res[0] = (perm & 0x1) ? 'x' : '-';
    res[1] = (perm & 0x4) ? 'r' : '-';
    res[2] = (perm & 0x2) ? 'w' : '-';
    res[3] = '\0';
}

err_t list_dirents(ino_id_t ino_id, int out_fd) {
    (void) out_fd;

    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    block_no_t curr_block_no = get_first_block(ino_id);
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
                return SUCCESS;
            }
            attributes_t metadata;
            err = get_inode_metadata(dir[i].ino_id, &metadata);
            if (err != SUCCESS) {
                kfree(dir);
                return err;
            }
            char perm_str[4];
            format_chmod_str(metadata.perm, perm_str);
            int size = dir[i].ino_id == 0 ? 0 : get_file_size_by_id(dir[i].ino_id);
            printf("%u %s %u tick=%u %s\n",
                   dir[i].ino_id,
                   perm_str,
                   size,
                   (unsigned int)metadata.mtime,
                   dir[i].name);
            i++;
        } 
        curr_block_no = get_ith_block_of_file_by_id(ino_id, ++index);
    }
    return SUCCESS;
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
