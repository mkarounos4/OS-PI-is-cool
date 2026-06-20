#include <sys/types.h>

#include "cmds.h"
#include "disk.h"
#include "kapi.h"
#include "scheduler/scheduler.h"

int open(const char *fname, int mode) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    int fd = k_open(fname, mode);
    if (fd < 0) {
        return fd;
    }

    int new_fd = -1;
    for (int i = 0; i < vec_len(&pcb->file_descriptors); i++) {
        if (vec_get(&pcb->file_descriptors, i) == -1) {
            vec_set(&pcb->file_descriptors, i, (void *)(uintptr_t)fd);
            new_fd = i;
            break;
        }
    }

    if (new_fd == -1) {
        vec_push_back(&pcb->file_descriptors, (void *)(uintptr_t)fd);
        new_fd = vec_len(&pcb->file_descriptors)-1;
    }

    return new_fd;
}

int close(int fd) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (vec_len(&pcb->file_descriptors) < fd || fd < 0) {
        return INVALID_ARGS;
    }

    err_t err = k_close((int)(uintptr_t)vec_get(&pcb->file_descriptors, fd));
    if (err) {
        return err;
    }
    vec_set(&pcb->file_descriptors, -1, (void *)(uintptr_t)fd);
}

int read(int fd, char *buf, int n) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (vec_len(&pcb->file_descriptors) < fd || fd < 0) {
        return INVALID_ARGS;
    }

    return k_read((int)(uintptr_t)vec_get(&pcb->file_descriptors, fd), buf, n);
}

int write(int fd, char *buf, int n) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (vec_len(&pcb->file_descriptors) < fd || fd < 0) {
        return INVALID_ARGS;
    }

    return k_write((int)(uintptr_t)vec_get(&pcb->file_descriptors, fd), buf, n);
}

int lseek(int fd, int offset, int whence) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (vec_len(&pcb->file_descriptors) < fd || fd < 0) {
        return INVALID_ARGS;
    }

    return k_lseek((int)(uintptr_t)vec_get(&pcb->file_descriptors, fd), offset, whence);
}

// Throws FS_not_mounted, k_open errors, k_close errors, k_update_file_time errors
err_t touch(char **file_paths) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    while (file_paths[0] != NULL) {
        // Open to create file if doesn't exist, and make sure valid name and path
        int i = open(file_paths[0], 0);
        if (i < 0) {
            return i;
        }
        if ((i = close(i)) < 0) {
            return i;
        }

        // Update file time
        if ((i = k_update_file_time(file_paths[0])) < 0) {
            return i;
        }

        // Next file
        file_paths++;
    }

    return SUCCESS;
}

err_t fs_mkdir(char **file_paths) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    while (file_paths[0] != NULL) {
        // Open to create file if doesn't exist, and make sure valid name and path
        int i = k_make_directory(file_paths[0]);
        if (i < 0) {
            return i;
        }

        // Next file
        file_paths++;
    }

    return SUCCESS;
}

// FS_NOT_MOUNTED, k_rename_file errors
err_t mv(char *src_path, char *dest_path) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    if (!strcmp(src_path, dest_path)) {
        return k_update_file_time(src_path);
    }

    if (k_check_if_exists(src_path) == FILE_NOT_FOUND) {
        return FILE_NOT_FOUND;
    }

    err_t err;
    err = k_unlink(dest_path);
    if (err && err != FILE_NOT_FOUND) {
        return err;
    }
    
    err = k_mv_file(src_path, dest_path);
    return err;
}

// Returns k_remove_file_if_exxists and k_unlink errors
err_t rm(char **file_paths) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }
    int i = 0;
    while (file_paths[i] != NULL) {
        err_t err;
        if ((err = k_unlink(file_paths[i])) != SUCCESS) {
            return err; 
        };
        i++;
    }

    return SUCCESS;
}

static err_t cat_check_same_input_output(char **file, char *output_file) {
    if (output_file == NULL || file == NULL) {
        return SUCCESS;
    }

    struct fs_dirent output_dirent;
    ino_id_t output_parent;
    err_t output_err = get_dirent_by_path(output_file, &output_dirent, FILE_TYPE, &output_parent, NULL);
    if (output_err == FILE_NOT_CREATED) {
        return SUCCESS;
    }
    if (output_err != SUCCESS) {
        return output_err;
    }

    for (int i = 0; file[i] != NULL; i++) {
        struct fs_dirent input_dirent;
        ino_id_t input_parent;
        err_t input_err = get_dirent_by_path(file[i], &input_dirent, FILE_TYPE, &input_parent, NULL);
        if (input_err != SUCCESS) {
            return input_err == FILE_NOT_CREATED ? FILE_NOT_FOUND : input_err;
        }
        if (input_parent == output_parent && input_dirent.ino_id == output_dirent.ino_id) {
            return CAT_SAME_INPUT_OUTPUT;
        }
    }

    return SUCCESS;
}

err_t cat(char **file, char *output_file, int flag) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    err_t same_file_err = cat_check_same_input_output(file, output_file);
    if (same_file_err != SUCCESS) {
        return same_file_err;
    }
    
    int out_fd = 1;
    if (output_file != NULL) {
        out_fd = open(output_file, flag);
    } 

    if (out_fd < 0) {
        return out_fd;
    }

    const int BUFF_SIZE = get_bytes_per_block();
    int use_stdin = (file == NULL || file[0] == NULL);
    while (use_stdin || (file != NULL && file[0] != NULL)) {
        int in_fd;
        int reading_stdin = 0;

        if (use_stdin) {
            in_fd = 0;
            reading_stdin = 1;
        } else {
            if (!k_check_if_exists(file[0])) {
                return FILE_NOT_FOUND;
            }
            in_fd = open(file[0], F_READ);
        }
        if (in_fd < 0) {
            if (out_fd != 1) {
                close(out_fd);
            }
            return in_fd;
        }

        char *data_read = kmalloc(BUFF_SIZE);
        if (data_read == NULL) {
            if (in_fd != 0) {
                close(in_fd);
            }
            if (out_fd != 1) {
                close(out_fd);
            }
            return FILE_READ_ERROR;
        }

        int bytes_read;
        do {
            bytes_read = read(in_fd, data_read, BUFF_SIZE);
            if (bytes_read < 0) {
                if (in_fd != 0) {
                    close(in_fd);
                }
                if (out_fd != 1) {
                    close(out_fd);
                }
                kfree(data_read);
                return FILE_READ_ERROR;
            }
            if (bytes_read > 0) {
                int bytes_written = write(out_fd, data_read, bytes_read);
                if (bytes_written != bytes_read) {
                    if (in_fd != 0) {
                        close(in_fd);
                    }
                    if (out_fd != 1) {
                        close(out_fd);
                    }
                    kfree(data_read);
                    return FILE_WRITE_ERROR;
                }
            }
        } while (bytes_read > 0);
        kfree(data_read);

        if (in_fd != 0) {
            close(in_fd);
        }
        
        if (!reading_stdin) {
            file++;
        } else {
            use_stdin = 0;
        }
    }

    if (output_file != NULL) {
        close(out_fd);
    }

    return SUCCESS;
}

err_t cp(char *src_path, char *dest_path, int flag) {
    (void)flag;

    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    if (!k_check_if_exists(src_path)) {
        return FILE_NOT_FOUND;
    }

    int src_fd;
    int dest_fd;

    src_fd = open(src_path, F_READ);

    dest_fd = open(dest_path, F_WRITE);

    const int BUF_SIZE = 1024;
    char* buf = kmalloc(BUF_SIZE);

    while (1) {
        int bytes_read;
        
        bytes_read = read(src_fd, buf, BUF_SIZE);
        if (bytes_read < 0) {
            return bytes_read;
        } else if (bytes_read == 0) {
            close(src_fd);
            close(dest_fd);
            return 0;
        } else {
            int bytes_written;
            bytes_written = write(dest_fd, buf, MIN(bytes_read, BUF_SIZE));
            if (bytes_written < 0) {
                close(src_fd);
                close(dest_fd);
                return bytes_written;
            }
        }
    }

    return SUCCESS;
}

err_t fs_chmod(char *file_name, char *new_perms, int flag) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    if (!k_check_if_exists(file_name)) {
        return FILE_NOT_FOUND;
    }

    uint8_t perm_to_add = 0;
    int perm_as_int;
    int err = atoi(new_perms, &perm_as_int);

    if (err == SUCCESS) {
        perm_to_add = perm_as_int;
        if (perm_to_add > 7) {
            return INVALID_ARGS;
        }
    } else {
        while (*new_perms != '\0') {
            if (new_perms[0] == 'x') {
                perm_to_add |= 0x01;
            } else if (new_perms[0] == 'r') {
                perm_to_add |= 0x04;
            } else if (new_perms[0] == 'w') {
                perm_to_add |= 0x02;
            } else {
                return INVALID_ARGS;
            }
            new_perms++;
        }
    }

    return k_chmod(file_name, perm_to_add, flag);
}

err_t ls(char *dir_path, int out_fd) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }
    err_t res = k_ls(dir_path, out_fd);
    return res;
}

err_t cd(char *path) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    err_t res = k_change_directory(path);
    return res;
}
