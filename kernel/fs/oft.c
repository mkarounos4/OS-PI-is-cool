#include "oft.h"

static Vec open_file_table;
static int oft_initialized = 0;

static char *copy_oft_name(const char *name) {
    size_t len = 0;
    while (name[len] != '\0') {
        len++;
    }

    char *copy = kmalloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    for (size_t i = 0; i <= len; i++) {
        copy[i] = name[i];
    }
    return copy;
}

static void entry_deletor(void *entry) {
    if (entry != NULL) {
        struct oft_entry *oentry = (struct oft_entry*) entry;
        kfree(oentry->file_name);
        if (oentry->ino_id > 0) {
            remove_ref_from_cache(oentry->ino_id);
        }
        kfree(entry);
    }
}

err_t initialize_oft() {
    if (oft_initialized) {
        empty_oft();
    }

    open_file_table = vec_new(3, entry_deletor);
    oft_initialized = 1;

    return SUCCESS;
}

err_t empty_oft(void) {
    if (!oft_initialized) {
        return SUCCESS;
    }

    vec_destroy(&open_file_table);
    oft_initialized = 0;
    return SUCCESS;
}

int oft_open_file(int mode, const char *file_name, ino_id_t ino_id, ino_id_t dir_block) {
    int oft_id;
    int err = find_file_in_table(ino_id, file_name, dir_block, mode, &oft_id);

    if (err == -2) {
        return F_ONLY_ONE_WRITER;
    } else if (err == SUCCESS) {
        struct oft_entry *entry;
        err = get_oft_entry_by_fd(oft_id, &entry);
        if (err) {
            return err;
        }
        entry->ref_count++;
        return oft_id;
    } else {
        // if file we're trying to open doesn't have a dirent yet (i.e. id_in_fs is 0, do that)
        struct oft_entry *new_entry = kmalloc(sizeof(struct oft_entry));
        *new_entry = (struct oft_entry) {
            .mode = mode,
            .cursor = 0,
            .ref_count = 1,
            .ino_id = ino_id,
            .inode = NULL,
            .parent_id = dir_block,
            .file_name = kmalloc(sizeof(char) * (strlen(file_name)+1))
        };
        
        // Create new file if applicable
        if (ino_id == 0) {
            // Adds new dirent to end of directory 
            struct fs_dirent dir;
            err = get_dirent_by_f_name(file_name, FILE_TYPE, &dir, dir_block);
            if (err != SUCCESS) {
                add_new_file(&new_entry, FILE_TYPE, 6);
                err = add_dirent(file_name, new_entry->ino_id, dir_block);
                if (err) {
                    return err;
                }
            }
        } else {
            new_entry->inode = get_inode_from_cache(ino_id);
        }

        // Updates file name and adds to open_file_table
        strcpy(new_entry->file_name, file_name);
        new_entry->file_name[strlen(file_name)] = '\0';
        if (oft_id == vec_len(&open_file_table) || oft_id == -1) {
            oft_id = vec_len(&open_file_table);
            vec_push_back(&open_file_table, new_entry);
        } else {
            vec_set(&open_file_table, oft_id, new_entry);
        }
    } 

    return oft_id;
}

int oft_add_reference(int fd) {
    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err) {
        return err;
    }

    entry->ref_count++;
    return SUCCESS;
}

int oft_close_file(int oft_id) {
    if (oft_id > vec_len(&open_file_table)) {
        return FILE_NOT_FOUND;
    }

    void *elem_void = vec_get(&open_file_table, oft_id);
    if (elem_void == NULL) {
        return FILE_NOT_FOUND;
    }

    struct oft_entry *entry = (struct oft_entry*) elem_void;
    entry->ref_count--;
    if (entry->ref_count == 0) {
        if (vec_len(&open_file_table) == oft_id + 1) {
            vec_pop_back(&open_file_table, NULL);
            entry_deletor(elem_void);
        } else {
            vec_set(&open_file_table, oft_id, NULL);
        }
    }

    return SUCCESS;
}

int find_file_in_table(ino_id_t ino_id, const char *file_name, ino_id_t parent_id, int mode, int *oft_id) {
    if (oft_id != NULL)  *oft_id = -1;
    for (int i = 0; i < vec_len(&open_file_table); i++) {
        struct oft_entry *next_entry = vec_get(&open_file_table, i);
        if (next_entry == NULL) {
            if (oft_id != NULL && *oft_id == -1) {
                *oft_id = i;
            }
            continue;
        }

        int same_file = 0;
        if (ino_id == 0 && next_entry->ino_id == 0) {
            same_file = next_entry->parent_id == parent_id &&
                        strcmp(next_entry->file_name, file_name) == 0;
        } else {
            same_file = ino_id == next_entry->ino_id;
        }

        if (same_file) {
            if ((mode & O_WRONLY) && (next_entry->mode & O_WRONLY)) {
                return F_ONLY_ONE_WRITER;
            }
            if (oft_id != NULL) *oft_id = i;
            return SUCCESS;
        }
    }
    if (oft_id != NULL) *oft_id = vec_len(&open_file_table);
    return FILE_NOT_FOUND;
}

err_t get_oft_entry_by_fd(int fd, struct oft_entry** entry_res) {
    if (fd < 0 || fd >= vec_len(&open_file_table)) {
        return OFT_FD_DOES_NOT_EXIST;
    } 

    *entry_res = (struct oft_entry*) vec_get(&open_file_table, fd);
    return *entry_res == NULL ? OFT_FD_DOES_NOT_EXIST : SUCCESS;
}
