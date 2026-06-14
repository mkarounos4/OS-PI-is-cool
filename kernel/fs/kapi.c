#include "kapi.h"

int k_open(const char *fname, int mode) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    // Check if filename is valid
    const char *curr = fname;
    while (curr[0] != '\0') {
        if ((curr[0] <= 'z' && curr[0] >= 'a') || (curr[0] <= 'Z' && curr[0] >= 'A') ||
                (curr[0] <= '9' && curr[0] >= '0') || curr[0] == '.' || curr[0] == '-' || curr[0] == '_' || curr[0] == '/') {
            curr++;
        } else {
            return INVALID_FILE_NAME;
        }
    }
    
    struct fs_dirent dirent;
    ino_id_t parent_dir_id;
    char *actual_name;
    err_t error = get_dirent_by_path(fname, &dirent, FILE_TYPE, &parent_dir_id, &actual_name);
    if (error == FILE_NOT_FOUND) {
        return FILE_NOT_FOUND;
    }

    if (error != FILE_NOT_CREATED) {
        if (!(dirent.perm & 0x4) && mode == F_READ) {
            return INVALID_PERMISSIONS;
        } else if (!(dirent.perm & 0x2) && (mode == F_WRITE || mode == F_APPEND)) {
            return INVALID_PERMISSIONS;
        }
    }

    // Create open or create new file and then return fd
    int fd = oft_open_file(mode, actual_name, error != FILE_NOT_CREATED ? dirent.ino_id: 0, parent_dir_id);
    // Truncate on write if not a new file
    if (fd > 0 && error != FILE_NOT_CREATED && mode == F_WRITE && dirent.ino_id != 0) {
        struct oft_entry *entry;
        err_t err = get_oft_entry_by_fd(fd, &entry);
        if (err) {
            return err;
        }
        err = clear_blocks_of_file(entry);
        if (err) {
            return err;
        }
        update_file_size(entry, 0);
        entry->cursor = 0;
        if (err) {
            return err;
        }
    }

    return fd;
}

int k_close(int fd) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    return oft_close_file(fd);
}

int k_read(int fd, char *buf, int n) {
    if (fd > -1 && fd < 3) {
        return read(fd, buf, n);
    }

    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    int tot_bytes_read = 0;
    struct oft_entry* entry;
    if (get_oft_entry_by_fd(fd, &entry) == OFT_FD_DOES_NOT_EXIST) {
        return OFT_FD_DOES_NOT_EXIST;
    }

    int size = get_file_size(entry);
    if (entry->cursor >= size) {
        return 0;
    }

    // Move real cursor to correct position in binary file, do special math for first offset.
    unsigned int curr_block_index = floor((float) entry->cursor / (float) get_bytes_per_block());
    int curr_block_no = get_ith_block_of_file(entry, curr_block_index);
    int remainder_offset = entry->cursor % get_bytes_per_block();
    int bytes_to_read = MIN(size - entry->cursor, MIN(n, get_bytes_per_block() - remainder_offset));
    int start = 1;
    
    char *data = malloc(get_bytes_per_block());
    while (n) {
        void *to_read;
        if (bytes_to_read < get_bytes_per_block()) {
            to_read = data;
        } else {
            to_read = buf;
        }
        err_t error;
        error = read_block(to_read, curr_block_no);

        if (error != SUCCESS) {
            free(data);
            return error;
        }

        if (bytes_to_read < get_bytes_per_block()) {
            if (start) {
                for (int i = 0; i < bytes_to_read; i++) {
                    buf[i] = data[remainder_offset + i];
                }
            } else {
                for (int i = 0; i < bytes_to_read; i++) {
                    buf[i] = data[i];
                }
            }
        }

        start = 0;
        entry->cursor += bytes_to_read;

        // If n = 0 or at end of file, we're done reading bytes.
        n -= bytes_to_read;
        tot_bytes_read += bytes_to_read;

        if (n == 0 || entry->cursor >= size) {
            free(data);
            return tot_bytes_read;
        }
        
        curr_block_index++;

        // Go to next block in FAT, update real cursor and number bytes to read.
        curr_block_no = get_ith_block_of_file(entry, curr_block_index);
        bytes_to_read = MIN(size - entry->cursor, MIN(n, get_bytes_per_block()));
    }
    
    free(data);
    return SUCCESS;
}

int k_file_add_reference(int fd) {
    return oft_add_reference(fd);
}

int k_write(int fd, char *buf, int n) {
    if (fd > -1 && fd < 3) {
        return write(fd, buf, n);
    }

    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    int tot_bytes_written = 0;
    struct oft_entry* entry;
    if (get_oft_entry_by_fd(fd, &entry) == OFT_FD_DOES_NOT_EXIST) {
        return OFT_FD_DOES_NOT_EXIST;
    }
    
    if (entry->ino_id == 0) {
        err_t err = add_new_file(&entry, FILE_TYPE);
        update_dirent_by_f_name(entry->file_name, entry->parent_id, FILE_TYPE, EDIT_ID, 0, 0, "", entry->ino_id);
        if (err) {
            return err;
        }
    }

    // Move real cursor to correct position in binary file, do special math for first offset.
    int size = get_file_size(entry);
    off_t offset;
    if (entry->mode == F_APPEND) {
        offset = size;
    } else {
        offset = entry->cursor;
    }

    unsigned int curr_block_index = floor((float) offset / (float) get_bytes_per_block());
    block_no_t curr_block_no = get_ith_block_of_file(entry, curr_block_index);
    if (curr_block_no == 0) {
        err_t err = allocate_new_block_for_file(entry, &curr_block_no);
        if (err) {
            return err;
        }
    }

    int remainder_offset = offset % get_bytes_per_block();

    int bytes_to_write = MIN(n, get_bytes_per_block() - remainder_offset);
    char *data = malloc(get_bytes_per_block());
    int start = 1;
    while (n) {
        char *to_write;
        if (curr_block_no == 0) {
            free(data);
            return FAT_NO_SPACE_REMAINING;
        }
        block_no_t block_to_write = curr_block_no;
        if (bytes_to_write < get_bytes_per_block()) {
            err_t err = read_block(data, block_to_write);
            if (err != SUCCESS) {
                return err;
            }
            if (start) {
                for (int i = 0; i < bytes_to_write; i++) {
                    data[remainder_offset + i] = buf[i];
                }
            } else {
                for (int i = 0; i < bytes_to_write; i++) {
                    data[i] = buf[i];
                }
            }
            to_write = data;
        } else {
            to_write = buf;
        }

        err_t err = write_block(to_write, curr_block_no);
        if (err != 0) {
            free(data);
            return err;
        }

        offset += bytes_to_write;
        entry->cursor = offset;

        struct fs_dirent dirent;
        err_t res;
        // TODO: Update size in dirent and time last edited.
        if ((res = get_dirent_by_f_name(entry->file_name, FILE_TYPE, &dirent, entry->parent_id)) != SUCCESS) {
            free(data);
            return res;
        }
        if (offset > size) {
            size = offset;
            res = update_file_size(entry, size);
            if (res != SUCCESS) {
                free(data);
                return res;
            }
        }
        
        // If n = 0, we're done writing bytes.
        n -= bytes_to_write;
        tot_bytes_written += bytes_to_write;
        if (n == 0) {
            free(data);
            return tot_bytes_written;
        }
        
        // Go to next block in FAT, update real cursor and number bytes to read.
        curr_block_no = get_ith_block_of_file(entry, ++curr_block_index);
        if (curr_block_no == 0) {
            err_t alloc_err = allocate_new_block_for_file(entry, &curr_block_no);
            if (alloc_err != SUCCESS) {
                free(data);
                return alloc_err;
            }
        }

        if (start) {
            buf += bytes_to_write;
        } else {
            buf += get_bytes_per_block();
        }
        start = 0;
        bytes_to_write = MIN(n, get_bytes_per_block());
    }

    free(data);
    return SUCCESS;
}

int k_lseek(int fd, int offset, int whence) {
    if (offset < 0) {
        return FILE_SEEK_ERROR;
    }

    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct oft_entry* entry;
    if (get_oft_entry_by_fd(fd, &entry) == OFT_FD_DOES_NOT_EXIST) {
        return OFT_FD_DOES_NOT_EXIST;
    }

    int file_size = get_file_size(entry);

    if (whence == F_SEEK_SET) {
        entry->cursor = offset;
    } else if (whence == F_SEEK_CUR) {
        entry->cursor += offset;
    } else if (whence == F_SEEK_END) {
        entry->cursor = file_size + offset;
        
    } else {
        return INVALID_ARGS;
    }
    
    // Allocate new blocks to fill hole if necessary
    int dif_until_new_block = file_size % get_bytes_per_block();
    offset = entry->cursor - file_size;
    block_no_t curr_block = get_ith_block_of_file(entry, ceil((float) file_size / (float) get_bytes_per_block()));

    // Fill hole in current block if applicable
    if (offset > 0 && dif_until_new_block != 0) {
       unsigned char *data = malloc(get_bytes_per_block());
       err_t error = read_block(data, curr_block);
       if (error) {
           free(data);
           return error;
       }
       int end = offset + file_size > get_bytes_per_block() ? get_bytes_per_block() : offset + file_size;
       for (int i = file_size; i < end; i++) {
            data[i] = '\0';
       }
       error = write_block(data, curr_block);
       free(data);
       if (error) {
           return error;
       }
       offset -= dif_until_new_block;
    }

    // Fill hole for new allocated blocks if applicable
    void *data = calloc(get_bytes_per_block(), sizeof(unsigned char));
    while (offset > 0) {
        err_t alloc_err = allocate_new_block_for_file(entry, &curr_block);
        if (alloc_err != SUCCESS) {
            free(data);
            return alloc_err;
        }
        write_block(data, curr_block); // fill hole with 0
        offset -= get_bytes_per_block();
    }
    free(data);

    // Update metadata with file size if necessary
    if (entry->cursor > file_size) {
        update_file_size(entry, entry->cursor);
    }

    return entry->cursor;
}

int k_chmod(const char *file_name, uint8_t new_perms, int flag) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    int new_flag = EDIT_PERM;
    if (flag == 0) {
        new_flag |= AND_PERM;
    } else if (flag == 1) {
        new_perms = ~new_perms;
        new_flag |= AND_PERM;
    }

    char *actual_name;
    struct fs_dirent dirent;
    uint16_t parent_dir;
    err_t err = get_dirent_by_path(file_name, &dirent, FILE_TYPE, &parent_dir, &actual_name);
    if (err) {
        return err;
    }

    err = update_dirent_by_f_name(actual_name, parent_dir, FILE_TYPE, new_flag, new_perms, 0, "", 0);
    free(actual_name);
    return err;
}

int k_update_file_time(const char *file_name) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    char *actual_name;
    struct fs_dirent dirent;
    uint16_t parent_dir;
    err_t err = get_dirent_by_path(file_name, &dirent, FILE_TYPE, &parent_dir, &actual_name);
    if (err) {
        return err;
    }
    err = update_dirent_by_f_name(actual_name, parent_dir, FILE_TYPE, 0, 0, 0, "", 0);
    free(actual_name);
    return err;
}

int k_unlink(const char*fname) {
    return free_file(fname);
}

int k_ls(const char *filename, int out_fs) {
    int dir_block = get_curr_dir();
    if (filename != NULL) {
        struct fs_dirent dir;
        err_t err = get_dirent_by_path(filename, &dir, DIRECTORY_F_TYPE, NULL, NULL);
        if (err) {
            return err;
        }
        dir_block = dir.ino_id;
    }

    err_t err = list_dirents(dir_block, out_fs);
    return err;
}

int k_mv_file(const char *src_path, const char *dest_path) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct fs_dirent old_dirent;
    ino_id_t parent_dir;
    int err = get_dirent_by_path(src_path, &old_dirent, FILE_TYPE, &parent_dir, NULL);
    if (err) {
        return err;
    }

    ino_id_t new_parent_dir;
    struct fs_dirent new_dirent;
    char *actual_name;
    err = get_dirent_by_path(dest_path, &new_dirent, FILE_TYPE, &new_parent_dir, &actual_name);
    if (err != FILE_NOT_CREATED && err) {
        return err;
    }

    err = add_dirent(actual_name, old_dirent.ino_id, FILE_TYPE, old_dirent.perm, new_parent_dir);
    if (err) {
        free(actual_name);
        return err;
    }

    err = remove_dirent_by_f_name_and_type(old_dirent.name, FILE_TYPE, parent_dir);
    if (err) {
        free(actual_name);
        return err;
    }

    free(actual_name);

    return err;
}

int k_check_if_exists(const char *f_name) {
    struct fs_dirent dir;
    return !get_dirent_by_path(f_name, &dir, FILE_TYPE, NULL, NULL);
}

int k_make_directory(char *f_path) {
    return add_dirent_by_path(f_path, DIRECTORY_F_TYPE, 0x7);
}

int k_change_directory(char *f_path) {
    struct fs_dirent dir;
    err_t err = get_dirent_by_path(f_path, &dir, DIRECTORY_F_TYPE, NULL, NULL);
    if (err) {
        return err;
    }

    set_curr_dir(dir.ino_id);
    return SUCCESS;
}

bool k_check_if_executable(char *f_name) {
    struct fs_dirent dir;
    if (!get_dirent_by_path(f_name, &dir, FILE_TYPE, NULL, NULL)) {
        return (dir.perm & 0x1);
    }
    return false;
}
