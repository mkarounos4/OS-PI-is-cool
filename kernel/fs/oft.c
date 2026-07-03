#include "oft.h"

static Vec open_file_table;
static int oft_initialized = 0;

static void entry_deletor(void *entry) {
    if (entry != NULL) {
        struct oft_entry *oentry = (struct oft_entry*) entry;
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
    int err = find_file_in_table(&oft_id);

    // if file we're trying to open doesn't have a dirent yet (i.e. id_in_fs is 0, do that)
    struct oft_entry *new_entry = kmalloc(sizeof(struct oft_entry));
    *new_entry = (struct oft_entry) {
        .mode = mode,
        .cursor = 0,
        .ref_count = 1,
        .ino_id = ino_id,
        .inode = NULL,
    };
    
    // Create new file if applicable
    if (ino_id == 0) {
        // Adds new dirent to end of directory 
        struct fs_dirent dir;
        err = get_dirent_by_f_name(file_name, FILE_TYPE, &dir, dir_block);
        if (err != SUCCESS) {
            err = add_new_file(&new_entry, FILE_TYPE, 6, get_default_fops());
            if (err < 0) {
                kfree(new_entry);
                return err;
            }
            err = add_dirent(file_name, new_entry->ino_id, dir_block);
            if (err) {
                remove_ref_from_cache(new_entry->ino_id);
                kfree(new_entry);
                return err;
            }
        }
    } else {
        new_entry->inode = get_inode_from_cache(ino_id);
    }

    if (new_entry->inode == NULL) {
        kfree(new_entry);
        return FILE_READ_ERROR;
    }

    // adds to open_file_table
    if (oft_id == -1 || (size_t)oft_id == vec_len(&open_file_table)) {
        oft_id = (int)vec_len(&open_file_table);
        vec_push_back(&open_file_table, new_entry);
    } else {
        vec_set(&open_file_table, oft_id, new_entry);
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

int oft_close_file(struct oft_entry *entry) {
    if (entry == NULL) {
        return INVALID_ARGS;
    }

    int oft_id = -1;
    for (size_t i = 0; i < vec_len(&open_file_table); i++) {
        void *elem_void = vec_get(&open_file_table, i);
        if (elem_void == NULL) {
            continue;
        }

        struct oft_entry *curr = (struct oft_entry*) elem_void;
        if (curr != entry) {
            continue;
        }

        oft_id = (int)i;
        break;
    }

    if (oft_id == -1) {
        return FILE_NOT_FOUND;
    }

    entry->ref_count--;
    if (entry->ref_count == 0) {
        if (vec_len(&open_file_table) == (size_t)oft_id + 1) {
            vec_pop_back(&open_file_table, NULL);
            entry_deletor((void*)entry);
        } else {
            vec_set(&open_file_table, oft_id, NULL);
        }
    }

    return SUCCESS;
}

int find_file_in_table(int *oft_id) {
    if (oft_id != NULL)  *oft_id = -1;
    for (size_t i = 0; i < vec_len(&open_file_table); i++) {
        struct oft_entry *next_entry = vec_get(&open_file_table, i);
        if (next_entry == NULL) {
            if (oft_id != NULL && *oft_id == -1) {
                *oft_id = i;
            }
        }
    }
    if (oft_id != NULL) *oft_id = (int)vec_len(&open_file_table);
    return FILE_NOT_FOUND;
}

err_t get_oft_entry_by_fd(int fd, struct oft_entry** entry_res) {
    if (fd < 0 || (size_t)fd >= vec_len(&open_file_table)) {
        return OFT_FD_DOES_NOT_EXIST;
    } 

    *entry_res = (struct oft_entry*) vec_get(&open_file_table, fd);
    return *entry_res == NULL ? OFT_FD_DOES_NOT_EXIST : SUCCESS;
}
