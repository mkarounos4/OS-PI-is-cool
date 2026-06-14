#include "../../headers/fat-helpers/disk.h"

static int is_mounted = 0;
static char *mounted_file_name = NULL;
static int mounted_file_d = -1;
static int bytes_per_block = 0;
static int num_table_blocks = 0;
static uint16_t curr_dir = 1;

err_t mkfs(const char *fs_name, int blocks_in_fat, int block_size_config) {
    // Create disk image
    int file_d = open(fs_name, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (file_d == -1) {
        return FILE_OPEN_ERROR;
    }

    int new_bytes_per_block;

    // Get bytes per block
    if (block_size_config == 0) {
        new_bytes_per_block = 256;
    } else if (block_size_config == 1) {
        new_bytes_per_block = 512;
    } else if (block_size_config == 2) {
        new_bytes_per_block = 1024;
    } else if (block_size_config == 3) {
        new_bytes_per_block = 2048;
    } else if (block_size_config == 4) {
        new_bytes_per_block = 4096;
    } else {
        close(file_d);
        return INVALID_ARGS;
    }

    curr_dir = 1;

    return mkfs_inode(file_d, blocks_in_fat, new_bytes_per_block);
}

err_t mount(const char *fs_name) {
    // If already mounted this file, return error
    if (is_mounted && strcmp(mounted_file_name, fs_name) == 0) {
        return FS_ALREADY_MOUNTED;
    }

    // If mounted other file, unmount before remounting
    if (is_mounted) {
        unmount();
    }

    // Open new file
    mounted_file_d = open(fs_name, O_RDWR);
    if (mounted_file_d == -1) {
        return FILE_OPEN_ERROR;
    }

    // Update mounted_file_name
    mounted_file_name = malloc(sizeof(char) * (strlen(fs_name)+1));
    strcpy(mounted_file_name, fs_name);
    mounted_file_name[strlen(fs_name)] = '\0';

    // Determine file system type and call the designated mount
    err_t err_code;
    is_mounted = 1;
    uint16_t indicator;
    read(mounted_file_d, &indicator, sizeof(uint16_t));
    lseek(mounted_file_d, 0, SEEK_SET);
    err_code = mount_inode(&bytes_per_block, &num_table_blocks);
    get_inode_from_cache(1);

    initialize_oft();

    // Close on any errors
    if (err_code) {
        close(mounted_file_d);
        free(mounted_file_name);
        mounted_file_d = -1;
        mounted_file_name = NULL;
        is_mounted = 0;
    }

    return err_code;
}

err_t unmount() {
    if (!is_mounted) {
        return FS_NOT_MOUNTED;
    }

    err_t err_code = lru_cache_empty();
    if (err_code) {
        return err_code;
    }
    
    err_code = unmount_inode();
    is_mounted = 0;
    if (err_code != SUCCESS) {
        return err_code;
    }

    close(mounted_file_d);
    mounted_file_d = -1;
    free(mounted_file_name);
    mounted_file_name = NULL;

    return SUCCESS;
}

err_t read_block(void *data, block_no_t num) {
    unsigned char *bytes = (unsigned char*) data;
    struct node_st *block_node;
    err_t err_code = lru_cache_add_to_front(&block_node, num);
    if (err_code) {
        return err_code;
    }

    memcpy(bytes, block_node->data, get_bytes_per_block());

    return SUCCESS;
}

err_t write_block(void *data, block_no_t num) {
    err_t err = lru_cache_update_data(data, num);
    if (err) {
        return err;
    }
}

// Returns first 0 in bitmap of block at block_with_bitmap
err_t find_free_from_bitmap(unsigned int *free_block, block_no_t block_with_bitmap, int update_taken) {

    void *data = malloc(get_bytes_per_block());
    err_t err_read = read_block(data, block_with_bitmap);
    if (err_read != 0) {
        free(data);
        return err_read;
    }

    unsigned char *bitmap = (unsigned char *) data;
    int index_byte = 0;
    for (int i = 0; i < get_bytes_per_block(); i++, index_byte++) {
        if (bitmap[i] != 0xFF) {
            break;
        }
    }

    if (index_byte >= get_bytes_per_block()) {
        free(data);
        return NO_FREE_BLOCKS;
    }
    
    int index = 0;
    for (int i = 0; i < 8; i++, index++) {
        if (!(bitmap[index_byte] & (1 << (7-i)))) {
            if (update_taken) {
                bitmap[index_byte] |= (1 << (7-i));
                err_t err_code = write_block(bitmap, block_with_bitmap);
                if (err_code != SUCCESS) {
                    free(data);
                    return err_code;
                }
            }
            break;
        }
    }

    free(data);
    if (index == 8) {
        return NO_FREE_BLOCKS;
    }

    *free_block = 8 * index_byte + index + 1;
    return SUCCESS;
}

block_no_t get_ith_block_of_file_by_id(ino_id_t id, unsigned int block_num) {
    return get_block_num_from_inode_with_id(id, block_num);
}

block_no_t get_ith_block_of_file(struct oft_entry *entry, unsigned int block_num) {
    return get_block_num_from_inode(&entry->inode->inode, block_num);
}

block_no_t get_next_block_from_file(struct oft_entry *entry, int next_block_index) {
    return get_ith_block_of_file(entry, next_block_index);
}

err_t allocate_new_block_for_file(struct oft_entry *entry, block_no_t *block_num) {
    entry->inode->dirty = 1;
    return allocate_block_for_file_inode(&entry->inode->inode, block_num);
}

err_t allocate_new_block_for_file_from_id(uint16_t id_in_fs, uint16_t* allocated_block) {
    block_no_t new_block;
    err_t err;
    if ((err = allocate_block_for_file_inode_from_id(id_in_fs, &new_block)) != SUCCESS) {
        return err;
    }
    *allocated_block = new_block;
    return SUCCESS;
}

err_t free_file(const char* f_name) {
    struct fs_dirent dirent;
    char *actual_name;
    ino_id_t parent_dir;
    err_t error = get_dirent_by_path(f_name, &dirent, FILE_TYPE, &parent_dir, &actual_name);
    if (error == FILE_NOT_FOUND || error == FILE_NOT_CREATED) {
        return FILE_NOT_FOUND;
    } else if (error) {
        return error;
    }

    if (dirent.ino_id != 0) {

       error = free_file_inode(get_inode_from_cache(dirent.ino_id));
       if (error != SUCCESS) {
           return error;
       }
    }
    remove_ref_from_cache(dirent.ino_id);

    error = remove_dirent_by_f_name_and_type(actual_name, dirent.type, parent_dir);
    return error;
}

err_t clear_blocks_of_file(struct oft_entry *entry) {
    entry->inode->dirty = 1;
    return clear_blocks_of_inode(&entry->inode->inode, 1);
}

err_t add_new_file(struct oft_entry **entry, int file_type) {
    err_t error = add_new_file_inode(&(*entry)->ino_id, file_type);
    (*entry)->inode = get_inode_from_cache((*entry)->ino_id);
    if (error != SUCCESS) {
        return error;
    }
    return SUCCESS;
}

err_t add_new_file_with_id(block_no_t* new_block) {
    return add_new_file_inode(new_block, FILE_TYPE);
}

err_t remove_last_block(uint16_t id_in_fs) {
    return remove_last_block_inode(id_in_fs);
}

uint16_t get_first_block(uint16_t file_id) {
        return get_block_num_from_inode_with_id(file_id, 0);
}

int get_file_size(struct oft_entry *entry) {
    if (entry->inode == NULL) {
        return 0;
    }

    return entry->inode->inode.metadata.i_size;
}

int get_file_size_by_id(ino_id_t ino_id) {
    struct cached_inode_st *inode_cached = get_inode_from_cache(ino_id);
    if (inode_cached == 0) {
        return -1;
    }

    int to_ret = inode_cached->inode.metadata.i_size;
    err_t error = remove_ref_from_cache(ino_id);
    if (error) return error;
    return to_ret;
}

int update_file_size(struct oft_entry *entry, int new_size) {
    if (entry == NULL || entry->inode == NULL) {
        return -1;
    }

    entry->inode->dirty = 1;
    entry->inode->inode.metadata.i_size = new_size;
    return SUCCESS;
}

void toggle_in_bitmap_data(unsigned char *data, unsigned int idx) {
    data[idx / 8] ^= (1 << (7 - (idx % 8)));
}

// Getters and Setters
int get_num_table_blocks() {
    return num_table_blocks;
}

int get_bytes_per_block() {
    return bytes_per_block;
}

int get_mounted_file_d() {
    return mounted_file_d;
}

char *get_mounted_file_name() {
    return mounted_file_name;
}

int get_is_mounted() {
    return is_mounted;
}

uint16_t get_curr_dir() {
    return curr_dir;
}

void set_curr_dir(ino_id_t id) {
    remove_ref_from_cache(curr_dir);
    curr_dir = id;
    get_inode_from_cache(id);
}
