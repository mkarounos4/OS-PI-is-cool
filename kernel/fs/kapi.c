#include "kapi.h"
#include "disk.h"
#include "errors.h"
#include "inodes.h"
#include "uart/uart.h"
#include "oft.h"
#include "dirs.h"
#include "devices/devices.h"
#include "elf_loader.h"
#include "memory/page_table/page_table.h"
#include "scheduler/scheduler.h"
#include "pipe/pipe.h"
#include "symlink.h"
#include "string.h"
#include "virtual_fs.h"

int default_open(struct oft_entry *entry);
int default_read(struct oft_entry *entry, char *buf, size_t n);
int default_write(struct oft_entry *entry, const char *buf, size_t n);
int dir_open(struct oft_entry *entry);
int dir_close(struct oft_entry *entry);
int dir_lookup(const char* f_name, struct fs_dirent* dirent, int curr_dir);
int dir_readdir(struct oft_entry *dir, struct fs_dirent *out);

static struct file_operations default_ops = (struct file_operations) {
    .open = default_open,
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
    char *actual_name = NULL;
    err_t error = get_dirent_by_path(fname, &dirent, &parent_dir_id, &actual_name);
    if (error == FILE_NOT_FOUND) {
        return FILE_NOT_FOUND;
    }
    if (error != SUCCESS && error != FILE_NOT_CREATED) {
        return error;
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
        if (metadata.type == DIRECTORY_TYPE &&
            (mode & (O_WRONLY | O_APPEND | O_TRUNC))) {
            return IS_A_DIRECTORY;
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

    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->open != NULL) {
        err = entry->inode->inode.metadata.fops->open(entry);
        if (err) {
            oft_close_file(entry);
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

    if (entry->inode->inode.metadata.fops != NULL &&
        entry->inode->inode.metadata.fops->read != NULL) {
        return entry->inode->inode.metadata.fops->read(entry, buf, n);
    }
    if (entry->inode->inode.metadata.type == DIRECTORY_TYPE) {
        return IS_A_DIRECTORY;
    }
    return 0;
}

int default_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return INVALID_ARGS;
    }

    if (entry->mode & O_TRUNC) {
        err_t err = clear_blocks_of_file(entry);
        if (err != SUCCESS) {
            return err;
        }
        entry->mode &= ~O_TRUNC;
    }

    return SUCCESS;
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

    if (entry->inode->inode.metadata.fops != NULL && entry->inode->inode.metadata.fops->write != NULL) {
        return entry->inode->inode.metadata.fops->write(entry, buf, n);
    }
    if (entry->inode->inode.metadata.type == DIRECTORY_TYPE) {
        return IS_A_DIRECTORY;
    }
    return 0;
}

int default_write(struct oft_entry *entry, const char *buf, size_t n) {
    int tot_bytes_written = 0;

    // Move real cursor to correct position in binary file, do special math for first offset.
    int size = get_file_size(entry);
    uint32_t offset;
    if (entry->mode & O_APPEND) {
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

    char *actual_name;
    struct fs_dirent dirent;
    ino_id_t parent_dir;
    err_t err = get_dirent_by_path(file_name, &dirent, &parent_dir, &actual_name);
    if (err) {
        return err;
    }

    if (flag == 0) {
        err = update_inode_metadata(dirent.ino_id,
                                    INODE_EDIT_PERM | INODE_AND_PERM,
                                    0, 0, 0);
        if (err == SUCCESS) {
            err = update_inode_metadata(dirent.ino_id, INODE_EDIT_PERM,
                                        0, new_perms, 0);
        }
    } else if (flag == 1) {
        err = update_inode_metadata(dirent.ino_id,
                                    INODE_EDIT_PERM | INODE_AND_PERM,
                                    0, (uint8_t)~new_perms, 0);
    } else if (flag == 2) {
        err = update_inode_metadata(dirent.ino_id, INODE_EDIT_PERM,
                                    0, new_perms, 0);
    } else {
        err = INVALID_ARGS;
    }

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
    err_t err = get_dirent_by_path(file_name, &dirent, &parent_dir, &actual_name);
    if (err) {
        return err;
    }
    err = update_inode_metadata(dirent.ino_id, 0, 0, 0, 0);
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
        err_t err = get_dirent_by_path(filename, &dir, NULL, NULL);
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
    int err = get_dirent_by_path(src_path, &old_dirent, &parent_dir, NULL);
    if (err) {
        return err;
    }

    ino_id_t new_parent_dir;
    struct fs_dirent new_dirent;
    char *actual_name;
    err = get_dirent_by_path(dest_path, &new_dirent, &new_parent_dir, &actual_name);
    if (err != FILE_NOT_CREATED && err) {
        return err;
    }

    err = add_dirent(actual_name, old_dirent.ino_id, new_parent_dir);
    if (err) {
        kfree(actual_name);
        return err;
    }

    err = remove_dirent_by_f_name(old_dirent.name, parent_dir);
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
    return !get_dirent_by_path(f_name, &dir, NULL, NULL);
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

    return get_dirent_by_path(path, dirent, NULL, NULL);
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
    err_t err = get_dirent_by_path(f_path, &dir, NULL, NULL);
    if (err == FILE_NOT_CREATED) {
        return FILE_NOT_FOUND;
    }
    if (err) {
        return err;
    }
    struct cached_inode_st *inode;
    err = vfs_get_inode(dir.ino_id, &inode);
    if (err != SUCCESS) {
        return err;
    }

    struct oft_entry entry = {
        .mode = O_RDONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = dir.ino_id,
        .inode = inode,
    };
    struct file_operations *fops = inode->inode.metadata.fops;
    if (fops == NULL || fops->open == NULL || fops->lookup == NULL) {
        vfs_put_inode(inode);
        return NOT_A_DIRECTORY;
    }
    err = fops->open(&entry);
    if (err != SUCCESS) {
        vfs_put_inode(entry.inode);
        return err;
    }
    fops = entry.inode->inode.metadata.fops;
    if (fops == NULL || fops->lookup == NULL) {
        if (fops != NULL && fops->close != NULL) {
            fops->close(&entry);
        }
        vfs_put_inode(entry.inode);
        return NOT_A_DIRECTORY;
    }

    set_curr_dir(entry.ino_id);
    if (fops->close != NULL) {
        err = fops->close(&entry);
    }
    vfs_put_inode(entry.inode);
    if (err != SUCCESS) {
        return err;
    }
    return SUCCESS;
}

bool k_check_if_executable(char *f_name) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    struct fs_dirent dir;
    if (!get_dirent_by_path(f_name, &dir, NULL, NULL)) {
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

    tcb_t *main_thread = get_curr_thread();
    if (main_thread == NULL || main_thread->pcb != pcb) {
        if (vec_len(&pcb->tids) == 0) {
            return INVALID_ARGS;
        }
        main_thread = thread_get_by_tid((tid_t)(uintptr_t)vec_get(&pcb->tids, 0));
        if (main_thread == NULL) {
            return INVALID_ARGS;
        }
    }

    uint64_t frame_va = main_thread->ctx.x19;
    uint64_t kernel_stack_base = PROC_KERNEL_STACK_TOP - PROC_KERNEL_STACK_SIZE;
    uint64_t kernel_stack_page_va = frame_va & ~(PAGE_SIZE - 1);
    uint64_t frame_offset = frame_va - kernel_stack_page_va;
    if (frame_va < kernel_stack_base || frame_va >= PROC_KERNEL_STACK_TOP ||
        frame_offset >= PAGE_SIZE) {
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

static err_t make_absolute_path(const char *path, char **absolute_path) {
    if (path == NULL || path[0] == '\0' || absolute_path == NULL) {
        return INVALID_ARGS;
    }

    if (path[0] == '/') {
        char *absolute = kmalloc(strlen(path) + 1);
        if (absolute == NULL) {
            return NO_FREE_BLOCKS;
        }
        strcpy(absolute, path);
        *absolute_path = absolute;
        return SUCCESS;
    }

    char cwd[1024];
    err_t err = getcwd(cwd, sizeof(cwd));
    if (err != SUCCESS) {
        return err;
    }

    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);
    int needs_slash = strcmp(cwd, "/") != 0;
    char *absolute = kmalloc(cwd_len + (size_t)needs_slash + path_len + 1);
    if (absolute == NULL) {
        return NO_FREE_BLOCKS;
    }

    strcpy(absolute, cwd);
    size_t pos = cwd_len;
    if (needs_slash) {
        absolute[pos++] = '/';
    }
    for (size_t i = 0; i < path_len; i++) {
        absolute[pos++] = path[i];
    }
    absolute[pos] = '\0';
    *absolute_path = absolute;
    return SUCCESS;
}

static err_t write_symlink_target(ino_id_t ino, const char *target_path) {
    struct cached_inode_st *inode = get_inode_from_cache(ino);
    if (inode == NULL) {
        return FILE_READ_ERROR;
    }

    struct oft_entry entry = {
        .mode = O_WRONLY,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = ino,
        .inode = inode,
    };

    int written = default_write(&entry, target_path, strlen(target_path));
    remove_ref_from_cache(ino);
    if (written < 0) {
        return written;
    }
    return written == (int)strlen(target_path) ? SUCCESS : FILE_WRITE_ERROR;
}

int createlink(const char *create_path, const char *orig_path, int is_soft) {
    fs_dirent dirent;
    err_t err = get_dirent_by_path(orig_path, &dirent, NULL, NULL);
    if (err) {
        return err;
    }

    attributes_t orig_metadata;
    err = get_inode_metadata(dirent.ino_id, &orig_metadata);
    if (err) {
        return err;
    }

    char *actual_name = NULL;
    ino_id_t parent_dir;
    err = get_dirent_by_path(create_path, &dirent, &parent_dir, &actual_name);
    if (!err) {
        kfree(actual_name);
        return FILE_ALREADY_EXISTS;
    } else if (err != FILE_NOT_CREATED) {
        if (actual_name != NULL) {
            kfree(actual_name);
        }
        return err;
    }

    ino_id_t new_ino = dirent.ino_id;
    if (is_soft) {
        char *absolute_orig_path = NULL;
        err = make_absolute_path(orig_path, &absolute_orig_path);
        if (err) {
            kfree(actual_name);
            return err;
        }
        err = add_new_file_inode(&new_ino, SYMLINK_TYPE, orig_metadata.perm, SYMLINK_FOPS);
        if (err) {
            kfree(absolute_orig_path);
            kfree(actual_name);
            return err;
        }
        err = write_symlink_target(new_ino, absolute_orig_path);
        kfree(absolute_orig_path);
        if (err) {
            kfree(actual_name);
            return err;
        }
    }
    err = add_dirent(actual_name, new_ino, parent_dir);
    kfree(actual_name);
    return err;
}

int k_readlink(const char *path, char *buffer, size_t count) {
    if (!get_is_mounted()) {
        return FS_NOT_MOUNTED;
    }

    return symlink_readlink(path, buffer, count);
}
