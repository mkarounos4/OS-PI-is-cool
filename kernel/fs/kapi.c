#include "kapi.h"
#include "uart/uart.h"
#include "oft.h"
#include "dirs.h"
#include "devices/devices.h"
#include "elf_loader.h"
#include "memory/page_table/page_table.h"
#include "scheduler/scheduler.h"
#include "pipe/pipe.h"
#include "procfs.h"
#include "string.h"

int default_read(struct oft_entry *entry, char *buf, size_t n);
int default_write(struct oft_entry *entry, const char *buf, size_t n);
int dir_open(struct oft_entry *entry);
int dir_close(struct oft_entry *entry);
int dir_lookup(const char* f_name, uint8_t is_dir_type,
               struct fs_dirent* dirent, int curr_dir);
int dir_readdir(struct oft_entry *dir, struct fs_dirent *out);

static struct file_operations default_ops = (struct file_operations) {
    .open = NULL,
    .close = NULL,
    .read = default_read,
    .write = default_write,
    .lookup = NULL,
    .readdir = NULL,
    .getattr = NULL,
};

static struct file_operations default_dir_ops = (struct file_operations) {
    .open = dir_open,
    .close = dir_close,
    .read = NULL,
    .write = NULL,
    .lookup = dir_lookup,
    .readdir = dir_readdir,
    .getattr = NULL,
};

struct file_operations *get_default_fops() {
    return &default_ops;
}

struct file_operations *get_default_dir_fops() {
    return &default_dir_ops;
}

static void repair_default_fops(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return;
    }

    if (entry->inode->inode.metadata.fops != NULL) {
        return;
    }

    uint8_t type = entry->inode->inode.metadata.type;
    if (type == DIRECTORY_TYPE) {
        entry->inode->inode.metadata.fops = get_default_dir_fops();
    } else if (type == FILE_TYPE || type == SYMLINK_TYPE) {
        entry->inode->inode.metadata.fops = get_default_fops();
    }
}

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
    err_t error = get_dirent_by_path(fname, &dirent, 0, &parent_dir_id, &actual_name);
    if (error == FILE_NOT_FOUND) {
        return FILE_NOT_FOUND;
    }

    if (error != FILE_NOT_CREATED) {
        attributes_t metadata;
        error = get_inode_metadata(dirent.ino_id, &metadata);
        if (error != SUCCESS) {
            return error;
        }
        if (!(metadata.perm & 0x4) && (mode & O_RDONLY)) {
            return INVALID_PERMISSIONS;
        } if (!(metadata.perm & 0x2) && ((mode & O_WRONLY) || (mode & O_APPEND))) {
            return INVALID_PERMISSIONS;
        }

        if ((mode & O_TRUNC) && !procfs_is_virtual_inode(dirent.ino_id)) {
            err_t err = clear_blocks_of_file_by_id(dirent.ino_id);
            if (err) {
                return err;
            }
        }
    } else if (!(mode & O_CREAT)) {
        return FILE_NOT_FOUND;
    }

    // Create open or create new file and then return fd
    int fd = oft_open_file(mode, actual_name, error != FILE_NOT_CREATED ? dirent.ino_id: 0, parent_dir_id);
    if (actual_name != NULL) {
        kfree(actual_name);
    }
    if (fd < 0) {
        return fd;
    }

    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err != SUCCESS) {
        return err;
    }

    repair_default_fops(entry);
    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->open != NULL) {
        err = entry->inode->inode.metadata.fops->open(entry);
        if (err) {
            return err;
        }
    }

    return fd;
}

int k_close(struct oft_entry *entry) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    repair_default_fops(entry);
    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->close != NULL) {
        err_t err = entry->inode->inode.metadata.fops->close(entry);
        if (err) {
            return err;
        }
    }

    return oft_close_file(entry);
}

int k_read(struct oft_entry *entry, char *buf, size_t n) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    if (!(entry->mode & O_RDONLY)) {
        return INVALID_PERMISSIONS;
    }

    repair_default_fops(entry);
    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->read != NULL) {
        return entry->inode->inode.metadata.fops->read(entry, buf, n);
    }
    return 0;
}

int default_read(struct oft_entry *entry, char *buf, size_t n) {
    int tot_bytes_read = 0;
    int size = get_file_size(entry);
    if (entry->cursor >= (uint32_t) size) {
        return 0;
    }

    // Move real cursor to correct position in binary file, do special math for first offset.
    unsigned int curr_block_index = entry->cursor / get_bytes_per_block();
    block_no_t curr_block_no = get_ith_block_of_file(entry, curr_block_index);
    int remainder_offset = entry->cursor % get_bytes_per_block();
    int bytes_to_read = MIN(size - (int)entry->cursor,
                            MIN((int)n, get_bytes_per_block() - remainder_offset));
    int start = 1;
    
    char *data = kmalloc(get_bytes_per_block());
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
            kfree(data);
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

        if (n == 0 || entry->cursor >= (uint32_t) size) {
            kfree(data);
            return tot_bytes_read;
        }
        
        curr_block_index++;

        // Go to next block in FAT, update real cursor and number bytes to read.
        curr_block_no = get_ith_block_of_file(entry, curr_block_index);
        bytes_to_read = MIN(size - (int)entry->cursor,
                            MIN((int)n, get_bytes_per_block()));
    }
    
    kfree(data);
    return SUCCESS;
}

int k_file_add_reference(int fd) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err != SUCCESS) {
        return err;
    }

    err = oft_add_reference(fd);
    if (err != SUCCESS) {
        return err;
    }

    repair_default_fops(entry);
    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->open != NULL) {
        return entry->inode->inode.metadata.fops->open(entry);
    }

    return SUCCESS;
}

int k_write(struct oft_entry *entry, const char *buf, size_t n) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }


    if (!(entry->mode & O_WRONLY)) {
        return INVALID_PERMISSIONS;
    }

    repair_default_fops(entry);
    if (entry->inode->inode.metadata.fops != NULL && entry->inode->inode.metadata.fops->write != NULL) {
        return entry->inode->inode.metadata.fops->write(entry, buf, n);
    }
    return 0;
}

int default_write(struct oft_entry *entry, const char *buf, size_t n) {
    int tot_bytes_written = 0;

    // Move real cursor to correct position in binary file, do special math for first offset.
    int size = get_file_size(entry);
    uint32_t offset;
    if (entry->mode == F_APPEND) {
        offset = size;
    } else {
        offset = entry->cursor;
    }

    unsigned int curr_block_index = offset / get_bytes_per_block();
    block_no_t curr_block_no = get_ith_block_of_file(entry, curr_block_index);
    if (curr_block_no == 0) {
        err_t err = allocate_new_block_for_file(entry, &curr_block_no);
        if (err) {
            return err;
        }
    }

    int remainder_offset = offset % get_bytes_per_block();

    int bytes_to_write = MIN((int)n, get_bytes_per_block() - remainder_offset);
    char *data = kmalloc(get_bytes_per_block());
    int start = 1;
    while (n) {
        const char *to_write;
        if (curr_block_no == 0) {
            kfree(data);
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

        err_t err = write_block((void *)to_write, curr_block_no);
        if (err != 0) {
            kfree(data);
            return err;
        }

        offset += bytes_to_write;
        entry->cursor = offset;

        if (offset > (uint32_t)size) {
            size = (int)offset;
            int res = update_file_size(entry, size);
            if (res != SUCCESS) {
                kfree(data);
                return res;
            }
        }
        
        // If n = 0, we're done writing bytes.
        n -= bytes_to_write;
        tot_bytes_written += bytes_to_write;
        if (n == 0) {
            kfree(data);
            return tot_bytes_written;
        }
        
        // Go to next block in FAT, update real cursor and number bytes to read.
        curr_block_no = get_ith_block_of_file(entry, ++curr_block_index);
        if (curr_block_no == 0) {
            err_t alloc_err = allocate_new_block_for_file(entry, &curr_block_no);
            if (alloc_err != SUCCESS) {
                kfree(data);
                return alloc_err;
            }
        }

        if (start) {
            buf += bytes_to_write;
        } else {
            buf += get_bytes_per_block();
        }
        start = 0;
        bytes_to_write = MIN((int)n, get_bytes_per_block());
    }

    kfree(data);
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
    unsigned int curr_block_index = (file_size + get_bytes_per_block() - 1) / get_bytes_per_block();
    block_no_t curr_block = get_ith_block_of_file(entry, curr_block_index);

    // Fill hole in current block if applicable
    if (offset > 0 && dif_until_new_block != 0) {
       unsigned char *data = kmalloc(get_bytes_per_block());
       err_t error = read_block(data, curr_block);
       if (error) {
           kfree(data);
           return error;
       }
       int end = offset + file_size > get_bytes_per_block() ? get_bytes_per_block() : offset + file_size;
       for (int i = file_size; i < end; i++) {
            data[i] = '\0';
       }
       error = write_block(data, curr_block);
       kfree(data);
       if (error) {
           return error;
       }
       offset -= dif_until_new_block;
    }

    // Fill hole for new allocated blocks if applicable
    void *data = kmalloc(get_bytes_per_block());
    kmemset(data, 0, get_bytes_per_block());
    while (offset > 0) {
        err_t alloc_err = allocate_new_block_for_file(entry, &curr_block);
        if (alloc_err != SUCCESS) {
            kfree(data);
            return alloc_err;
        }
        write_block(data, curr_block); // fill hole with 0
        offset -= get_bytes_per_block();
    }
    kfree(data);

    // Update metadata with file size if necessary
    if (entry->cursor > (uint32_t)file_size) {
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
    ino_id_t parent_dir;
    err_t err = get_dirent_by_path(file_name, &dirent, 0, &parent_dir, &actual_name);
    if (err) {
        return err;
    }

    err = update_inode_metadata(dirent.ino_id, EDIT_PERM, 0, new_perms);
    kfree(actual_name);
    return err;
}

int k_update_file_time(const char *file_name) {
    // Return if not mounted
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    char *actual_name;
    struct fs_dirent dirent;
    ino_id_t parent_dir;
    err_t err = get_dirent_by_path(file_name, &dirent, 0, &parent_dir, &actual_name);
    if (err) {
        return err;
    }
    err = update_inode_metadata(dirent.ino_id, 0, 0, 0);
    kfree(actual_name);
    return err;
}

int k_unlink(const char*fname) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    return free_file(fname);
}

int k_ls(const char *filename, int out_fs) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    ino_id_t dir_block = get_curr_dir();
    if (filename != NULL) {
        struct fs_dirent dir;
        err_t err = get_dirent_by_path(filename, &dir, 1, NULL, NULL);
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
    int err = get_dirent_by_path(src_path, &old_dirent, 0, &parent_dir, NULL);
    if (err) {
        return err;
    }

    ino_id_t new_parent_dir;
    struct fs_dirent new_dirent;
    char *actual_name;
    err = get_dirent_by_path(dest_path, &new_dirent, 0, &new_parent_dir, &actual_name);
    if (err != FILE_NOT_CREATED && err) {
        return err;
    }

    err = add_dirent(actual_name, old_dirent.ino_id, new_parent_dir);
    if (err) {
        kfree(actual_name);
        return err;
    }

    err = remove_dirent_by_f_name_and_type(old_dirent.name, FILE_TYPE, parent_dir);
    if (err) {
        kfree(actual_name);
        return err;
    }

    kfree(actual_name);

    return err;
}

int k_check_if_exists(const char *f_name) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct fs_dirent dir;
    return !get_dirent_by_path(f_name, &dir, 0, NULL, NULL);
}

static int k_resolve_path_any(const char *path, struct fs_dirent *dirent) {
    if (path == NULL || dirent == NULL || path[0] == '\0') {
        return INVALID_ARGS;
    }

    if (strcmp(path, "/") == 0) {
        dirent->ino_id = ROOT_INO;
        strcpy(dirent->name, "/");
        return SUCCESS;
    }

    err_t err = get_dirent_by_path(path, dirent, 0, NULL, NULL);
    if (err == SUCCESS) {
        return SUCCESS;
    }

    err = get_dirent_by_path(path, dirent, 1, NULL, NULL);
    if (err == SUCCESS) {
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

int k_stat(const char *path, struct fs_stat_st *stat) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }
    if (stat == NULL) {
        return INVALID_ARGS;
    }

    struct fs_dirent dirent;
    err_t err = k_resolve_path_any(path, &dirent);
    if (err != SUCCESS) {
        return err;
    }

    attributes_t metadata;
    err = get_inode_metadata(dirent.ino_id, &metadata);
    if (err != SUCCESS) {
        return err;
    }

    stat->ino_id = dirent.ino_id;
    stat->links_count = metadata.i_links_count;
    stat->type = metadata.type;
    stat->perm = metadata.perm;
    stat->size = metadata.i_size;
    stat->blocks = metadata.i_blocks;
    stat->mtime = metadata.mtime;
    stat->rdev_major = metadata.i_rdev.major;
    stat->rdev_minor = metadata.i_rdev.minor;
    return SUCCESS;
}

int k_make_directory(char *f_path) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    return add_dirent_by_path(f_path, DIRECTORY_TYPE, 0x7);
}

int k_change_directory(char *f_path) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct fs_dirent dir;
    err_t err = get_dirent_by_path(f_path, &dir, 1, NULL, NULL);
    if (err) {
        return err;
    }

    set_curr_dir(dir.ino_id);
    return SUCCESS;
}

bool k_check_if_executable(char *f_name) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct fs_dirent dir;
    if (!get_dirent_by_path(f_name, &dir, 0, NULL, NULL)) {
        attributes_t metadata;
        if (get_inode_metadata(dir.ino_id, &metadata) != SUCCESS) {
            return false;
        }
        return (metadata.perm & 0x1);
    }
    return false;
}

int k_exec(const char *path, char *const argv[], struct trap_frame *frame,
           struct trap_frame **next_frame) {
    pcb_t *pcb = get_curr_process();
    return elf_exec_process(pcb, path, argv, frame,
                            (uint64_t)(uintptr_t)frame, next_frame, 1);
}

int k_exec_process(int pid, const char *path, char *const argv[]) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return INVALID_ARGS;
    }

    thread_t *main_thread = &pcb->threads[0];
    uint64_t kernel_stack_page_va = PROC_KERNEL_STACK_TOP - PAGE_SIZE;
    uint64_t frame_va = main_thread->ctx.x19;
    uint64_t frame_offset = frame_va - kernel_stack_page_va;
    if (frame_offset >= PAGE_SIZE) {
        return INVALID_ARGS;
    }

    void *kernel_stack_page =
        pt_get_mapped_page((uint64_t *)(uintptr_t)main_thread->ctx.ttbr0_el1_va,
                           kernel_stack_page_va);
    if (kernel_stack_page == NULL) {
        return INVALID_ARGS;
    }

    struct trap_frame *frame =
        (struct trap_frame *)(uintptr_t)((uint8_t *)kernel_stack_page +
                                         frame_offset);
    return elf_exec_process(pcb, path, argv, frame, frame_va, NULL, 0);
}
