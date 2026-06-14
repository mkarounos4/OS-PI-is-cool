#include "inodes.h"

err_t mkfs_inode(int file_d, int blocks_in_fat, int bytes_per_block) {
    // Add superblock data
    unsigned char *data = malloc(bytes_per_block);
    for (int i = 0; i < bytes_per_block; i++) {
        data[i] = 0x00;
    }

    // Initialize Superblock
    struct superblock_st super;
    super.inodes_indicator = INODES_INDICATOR;
    super.bytes_per_block = bytes_per_block;
    super.num_fat_inode_blocks = blocks_in_fat;
    ssize_t bytes_written = write(file_d, &super, sizeof(struct superblock_st));
    if (bytes_written < sizeof(struct superblock_st)) {
        close(file_d);
        free(data);
        return FILE_WRITE_ERROR;
    }
    
    // Write padding for superblock's block
    ssize_t size_to_write = bytes_per_block - sizeof(struct superblock_st);
    bytes_written = write(file_d, data, size_to_write);
    if (bytes_written < size_to_write) {
        close(file_d);
        free(data);
        return FILE_WRITE_ERROR;
    }
    
    free(data);

    // Initializez block bitmap
    data = malloc(bytes_per_block);
    for (int i = 0; i < bytes_per_block; i++) {
        data[i] = 0x00;
    }

    for (int i = 0; i < blocks_in_fat + INODE_TABLE_START_BLOCK + 1; i++) {
        data[i / 8] |= (1 << (7-(i % 8)));
    }
    
    bytes_written = write(file_d, data, bytes_per_block);
    if (bytes_written < bytes_per_block) {
        close(file_d);
        free(data);
        return FILE_WRITE_ERROR;
    }
    
    // initialize inode bitmap
    data[0] = 0x80;
    for (int i = 1; i < bytes_per_block; i++) {
        data[i] = 0x00;
    }
    
    bytes_written = write(file_d, data, bytes_per_block);
    if (bytes_written < bytes_per_block) {
        close(file_d);
        free(data);
        return FILE_WRITE_ERROR;
    }

    // Initialize root directory inode
    struct inode_st *data_inodes = malloc(bytes_per_block);
    memset(data_inodes, 0, bytes_per_block);
    struct inode_st root_node;
    attributes_t meta = {
        .i_blocks = 1
    };
    root_node.metadata = meta;
    for (int i = 1; i < 12; i++) { root_node.blocks[i] = 0; }
    root_node.blocks[0] = INODE_TABLE_START_BLOCK + blocks_in_fat;
    data_inodes[0] = root_node;
    bytes_written = write(file_d, data_inodes, bytes_per_block);
    if (bytes_written < bytes_per_block) {
        close(file_d);
        free(data_inodes);
        free(data);
        return FILE_WRITE_ERROR;
    }

    data[0] = 0x00;
    for (int i = 1; i < blocks_in_fat; i++) {
        bytes_written = write(file_d, data, bytes_per_block);
        if (bytes_written < bytes_per_block) {
            close(file_d);
            free(data);
            return FILE_WRITE_ERROR;
        }
    }

    // add dirent root
    struct fs_dirent* data_root = malloc(bytes_per_block);
    memset(data_root, 0, bytes_per_block);
    time_t now = time(NULL);
    data_root[0] = (struct fs_dirent) {
        .name =  ".",
        .ino_id = 1,
        .type = DIRECTORY_F_TYPE,
        .perm = 0x7,
        .mtime = now,
    };
    data_root[1] = (struct fs_dirent) {
        .name =  "..",
        .ino_id = 1,
        .type = DIRECTORY_F_TYPE,
        .perm = 0x7,
        .mtime = now,
    };
    data_root[2] = (struct fs_dirent) {
        .name = "\0",
    };

    bytes_written = write(file_d, data_root, bytes_per_block);
    free(data_root);
    if (bytes_written < bytes_per_block) {
        close(file_d);
        free(data_inodes);
        free(data);
        return FILE_WRITE_ERROR;
    }

    // Initialize remaining blocks
    for (int i = INODE_TABLE_START_BLOCK + blocks_in_fat + 1; i < bytes_per_block * 8; i++) {
        bytes_written = write(file_d, data, bytes_per_block);
        if (bytes_written < bytes_per_block) {
            close(file_d);
            free(data_inodes);
            free(data);
            return FILE_WRITE_ERROR;
        }
    }

    free(data);
    free(data_inodes);
    close(file_d);

    return SUCCESS;
}

// Returns SUCCESS, FS_ALREADY_MOUNTED, FILE_OPEN_ERROR, FILE_READ_ERROR, unmount's errors, and get_inode's errors
err_t mount_inode(int *bytes_per_block, int *num_inode_blocks) {
    // Read in superblock
    struct superblock_st superblock;
    ssize_t bytes_read = read(get_mounted_file_d(), &superblock, sizeof(struct superblock_st));
    if (bytes_read < sizeof(struct superblock_st)) {
        return FILE_READ_ERROR;
    }

    *bytes_per_block = superblock.bytes_per_block;
    *num_inode_blocks = superblock.num_fat_inode_blocks;

    return SUCCESS;
}

err_t unmount_inode() {
    empty_inode_cache();

    return SUCCESS;
}

err_t get_inode_raw(struct inode_st *node, ino_id_t id) {
    block_no_t block_with_inode =
        (id - 1) / INODES_PER_BLOCK + INODE_TABLE_START_BLOCK;

    int inode_num_in_block =
        (id - 1) % INODES_PER_BLOCK;
    void *data = malloc(get_bytes_per_block());
    int err = read_block(data, block_with_inode);
    if (err != 0) {
        free(data);
        return err;
    }

    struct inode_st *inodes = (struct inode_st*) data;
    *node = inodes[inode_num_in_block];
    
    free(data);
    return SUCCESS;
}

err_t get_inode(struct cached_inode_st** node, ino_id_t id) {
    *node = get_inode_from_cache(id);
    return SUCCESS;
}

err_t write_inode(struct inode_st *node, ino_id_t id) {
    block_no_t block_with_inode =
        (id - 1) / INODES_PER_BLOCK + INODE_TABLE_START_BLOCK;

    int inode_num_in_block =
        (id - 1) % INODES_PER_BLOCK;
    struct inode_st *data = malloc(get_bytes_per_block());
    int err = read_block(data, block_with_inode);
    if (err != 0) {
        free(data);
        return err;
    }

    data[inode_num_in_block] = *node;
    err = write_block(data, block_with_inode);
    if (err != SUCCESS) {
        free(data);
        return err;
    }

    free(data);
    return SUCCESS;
}

err_t find_free_block(block_no_t *free_block, int update_taken) {
    unsigned int free_block_u = 0;
    err_t err_code = find_free_from_bitmap(&free_block_u, BLOCK_BITMAP, update_taken);
    if (err_code != SUCCESS) {
        return err_code;
    }
    *free_block = (block_no_t) free_block_u - 1;
    return SUCCESS;
}

err_t find_free_inode(ino_id_t *free_block, int update_taken) {
    unsigned int free_block_u = 0;
    err_t err_code = find_free_from_bitmap(&free_block_u, INODE_BITMAP, update_taken);
    if (err_code != SUCCESS) {
        return err_code;
    }
    *free_block = (ino_id_t) free_block_u;
    if (free_block_u >= INODES_PER_BLOCK * get_num_table_blocks()) {
        return NO_FREE_BLOCKS;
    }
    return SUCCESS;
}

block_no_t get_block_num_from_disk_ptr(block_no_t ptr_block_num, unsigned int desired_block_idx) {
    void *data = malloc(get_bytes_per_block());
    read_block(data, ptr_block_num);
    block_no_t *blocks_ptr_arr = (block_no_t*) data;
    block_no_t to_return = blocks_ptr_arr[desired_block_idx];
    free(data);
    return to_return;
}

block_no_t get_block_num_from_inode_with_id(ino_id_t id, unsigned int index) {
    struct cached_inode_st *inode = get_inode_from_cache(id);
    if (inode == NULL) return 0;
    block_no_t block = get_block_num_from_inode(&inode->inode, index);
    remove_ref_from_cache(id);
    return block;
}

block_no_t get_block_num_from_inode(struct inode_st *inode, unsigned int block_num) {
    if (inode->metadata.i_blocks <= block_num) {
        return 0;
    }

    if (block_num < 12 || inode->metadata.i_blocks <= 15) {
        return inode->blocks[block_num];
    }

    // Case for fits in single pointers
    if (block_num < 12 + BLOCKS_IN_SINGLE_PTR) {
        return get_block_num_from_disk_ptr(inode->blocks[12], block_num - 12);
    }

    // Case for fits in double pointers
    if (block_num < 12 + BLOCKS_IN_SINGLE_PTR + BLOCKS_IN_DOUBLE_PTR) {
        // Handle double pointer
        unsigned int double_ptr_with_block = block_num - BLOCKS_IN_SINGLE_PTR - 12;
        unsigned int ptr_with_block = double_ptr_with_block % BLOCKS_IN_DOUBLE_PTR;
        double_ptr_with_block /= BLOCKS_IN_DOUBLE_PTR;
        
        block_no_t single_ptr_block = get_block_num_from_disk_ptr(inode->blocks[13], double_ptr_with_block);
        return get_block_num_from_disk_ptr(single_ptr_block, ptr_with_block);
    }

    // Case for fits in triple pointers
    unsigned int triple_ptr_with_block = block_num - BLOCKS_IN_DOUBLE_PTR - BLOCKS_IN_SINGLE_PTR - 12;
    unsigned int double_ptr_with_block = triple_ptr_with_block % BLOCKS_IN_TRIPLE_PTR;
    triple_ptr_with_block /= BLOCKS_IN_TRIPLE_PTR;

    unsigned int ptr_with_block = double_ptr_with_block % BLOCKS_IN_DOUBLE_PTR;
    double_ptr_with_block /= BLOCKS_IN_DOUBLE_PTR;

    block_no_t double_ptr_block = get_block_num_from_disk_ptr(inode->blocks[14], triple_ptr_with_block);
    block_no_t ptr_block = get_block_num_from_disk_ptr(double_ptr_block, double_ptr_with_block);
    return get_block_num_from_disk_ptr(ptr_block, ptr_with_block);
}

err_t get_block_from_file_inode(void *data, struct inode_st *inode, unsigned int block_num) {
    return read_block(data, get_block_num_from_inode(inode, block_num));
}

err_t write_block_of_file_inode(void *data, struct inode_st *inode, unsigned int block_num) {
    return write_block(data, get_block_num_from_inode(inode, block_num));
}

err_t allocate_block_for_file_inode_from_id(ino_id_t id_in_fs, block_no_t *new_block) {
    struct cached_inode_st *inode_cached = get_inode_from_cache(id_in_fs);
    if (inode_cached == 0) {
        *new_block = 0;
        return -1;
    }

    err_t error = allocate_block_for_file_inode(&inode_cached->inode, new_block);
    inode_cached->dirty = 1;
    err_t error2 = remove_ref_from_cache(id_in_fs);
    if (error) return error;
    return error2;
}

err_t allocate_block_for_file_inode(struct inode_st *inode, block_no_t *returned_block_num) {
    if (inode->metadata.i_blocks == 12 + BLOCKS_IN_SINGLE_PTR + BLOCKS_IN_DOUBLE_PTR + BLOCKS_IN_TRIPLE_PTR) {
        return INODE_FULL;
    }

    block_no_t block_was_alloc;
    err_t err_code = find_free_block(&block_was_alloc, 1);
    if (err_code != SUCCESS) {
        return err_code;
    }

    if (inode->metadata.i_blocks < 15) {
        // Single block storage
        inode->blocks[inode->metadata.i_blocks] = block_was_alloc;
    } else if (inode->metadata.i_blocks < 12 + BLOCKS_IN_SINGLE_PTR) {
        block_no_t *ptr_data = malloc(get_bytes_per_block());
        
        // Create new single pointer if at 15
        if (inode->metadata.i_blocks == 15) {
            block_no_t ptr_block;
            err_code = find_free_block(&ptr_block, 1);
            if (err_code != SUCCESS) {
                return err_code;
            }

            for (int i = 12; i < 15; i++) {
                ptr_data[i-12] = inode->blocks[i];
            }
            inode->blocks[12] = ptr_block;
        } else {
            err_code = read_block(ptr_data, inode->blocks[12]);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }
        }

        // Add block to single pointer
        ptr_data[inode->metadata.i_blocks - 12] = block_was_alloc;
        err_code = write_block(ptr_data, inode->blocks[12]);
        free(ptr_data);
        if (err_code != SUCCESS) {
            return err_code;
        }
    } else if (inode->metadata.i_blocks < 12 + BLOCKS_IN_SINGLE_PTR + BLOCKS_IN_DOUBLE_PTR) {
        // Handle double pointer case
        unsigned int num_in_double_ptr = inode->metadata.i_blocks - 12 - BLOCKS_IN_SINGLE_PTR;
        unsigned int index_in_single_ptr = num_in_double_ptr % BLOCKS_IN_SINGLE_PTR;
        unsigned int single_ptr_idx_in_double_ptr = num_in_double_ptr / BLOCKS_IN_SINGLE_PTR;
        
        block_no_t *ptr_data = malloc(get_bytes_per_block());
        block_no_t single_ptr_block;
        
        // Create new single or double block if necessary
        if (index_in_single_ptr == 0) {
            err_code = find_free_block(&single_ptr_block, 1);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }
            
            block_no_t *double_ptr_data = malloc(get_bytes_per_block());
            if (single_ptr_idx_in_double_ptr == 0) {
                err_code = find_free_block(&inode->blocks[13], 1);
                if (err_code != SUCCESS) {
                    free(ptr_data);
                    free(double_ptr_data);
                    return err_code;
                }
            } else {
                err_code = read_block(double_ptr_data, inode->blocks[13]);
                if (err_code != SUCCESS) {
                    free(ptr_data);
                    free(double_ptr_data);
                    return err_code;
                }
            }

            double_ptr_data[single_ptr_idx_in_double_ptr] = single_ptr_block;
            err_code = write_block(double_ptr_data, inode->blocks[13]);
            free(double_ptr_data);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }
        } else {
            block_no_t *double_ptr_data = malloc(get_bytes_per_block());
            err_code = read_block(double_ptr_data, inode->blocks[13]);
            if (err_code != SUCCESS) {
                free(double_ptr_data);
                free(ptr_data);
                return err_code;
            }

            single_ptr_block = double_ptr_data[single_ptr_idx_in_double_ptr];
            err_code = read_block(ptr_data, single_ptr_block);
            free(double_ptr_data);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }
        }

        ptr_data[index_in_single_ptr] = block_was_alloc;
        err_code = write_block(ptr_data, single_ptr_block);
        free(ptr_data);
        if (err_code != SUCCESS) {
            return err_code;
        }
    } else {
        // triple pointer case
        unsigned int num_in_triple_ptr = inode->metadata.i_blocks - 12 - BLOCKS_IN_SINGLE_PTR - BLOCKS_IN_DOUBLE_PTR;
        unsigned int index_in_double_ptr = num_in_triple_ptr % BLOCKS_IN_DOUBLE_PTR;
        unsigned int double_ptr_idx_in_triple_ptr = num_in_triple_ptr / BLOCKS_IN_DOUBLE_PTR;

        unsigned int index_in_single_pointer = index_in_double_ptr % BLOCKS_IN_SINGLE_PTR;
        unsigned int single_ptr_idx_in_double_ptr = index_in_double_ptr / BLOCKS_IN_SINGLE_PTR;

        block_no_t *ptr_data = malloc(get_bytes_per_block());
        block_no_t single_ptr_block; 

        // Create new triple or double or single block if necessary
        if (index_in_single_pointer == 0) {
            // new single pointer
            err_code = find_free_block(&single_ptr_block, 1);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }

            block_no_t *double_ptr_data = malloc(get_bytes_per_block());
            block_no_t double_ptr_block;
            if (single_ptr_idx_in_double_ptr == 0) {
                // New double pointer
                err_code = find_free_block(&double_ptr_block, 1);
                if (err_code != SUCCESS) {
                    free(ptr_data);
                    free(double_ptr_data);
                    return err_code;
                }

                block_no_t *triple_ptr_data = malloc(get_bytes_per_block());
                if (double_ptr_idx_in_triple_ptr == 0) {
                    // New triple pointer
                    err_code = find_free_block(&inode->blocks[14], 1);
                    if (err_code != SUCCESS) {
                        free(triple_ptr_data);
                        free(double_ptr_data);
                        free(ptr_data);
                        return err_code;
                    }
                } else {
                    // Old triple pointer, to place new double pointer
                    err_code = read_block(triple_ptr_data, inode->blocks[14]);
                    if (err_code != SUCCESS) {
                        free(triple_ptr_data);
                        free(double_ptr_data);
                        free(ptr_data);
                        return err_code;
                    }
                }

                // Add double pointer to triple pointer block and write
                triple_ptr_data[double_ptr_idx_in_triple_ptr] = double_ptr_block;
                err_code = write_block(triple_ptr_data, inode->blocks[14]);
                free(triple_ptr_data);
                if (err_code != SUCCESS) {
                    free(double_ptr_data);
                    free(ptr_data);
                    return err_code;
                }
            } else {
                // Read data from existing double pointer block
                block_no_t *triple_ptr_data = malloc(get_bytes_per_block());
                err_code = read_block(triple_ptr_data, inode->blocks[14]);
                if (err_code != SUCCESS) {
                    free(triple_ptr_data);
                    free(double_ptr_data);
                    free(ptr_data);
                    return err_code;
                }

                double_ptr_block = triple_ptr_data[double_ptr_idx_in_triple_ptr];
                err_code = read_block(double_ptr_data, double_ptr_block);
                free(triple_ptr_data);
                if (err_code != SUCCESS) {
                    free(double_ptr_data);
                    free(ptr_data);
                    return err_code;
                }
            }

            double_ptr_data[single_ptr_idx_in_double_ptr] = single_ptr_block;
            err_code = write_block(double_ptr_data, double_ptr_block);
            free(double_ptr_data);
            if (err_code != SUCCESS) {
                free(ptr_data);
            }
        } else {
            // Read from existing single ptr block
            block_no_t *triple_ptr_data = malloc(get_bytes_per_block());
            err_code = read_block(triple_ptr_data, inode->blocks[14]);
            if (err_code != SUCCESS) {
                free(triple_ptr_data);
                free(ptr_data);
                return err_code;
            }

            block_no_t double_ptr_block = triple_ptr_data[double_ptr_idx_in_triple_ptr];
            free(triple_ptr_data);

            
            block_no_t *double_ptr_data = malloc(get_bytes_per_block());
            err_code = read_block(double_ptr_data, double_ptr_block);
            if (err_code != SUCCESS) {
                free(double_ptr_data);
                free(ptr_data);
                return err_code;
            }

            single_ptr_block = double_ptr_data[single_ptr_idx_in_double_ptr];
            free(double_ptr_data);

            err_code = read_block(ptr_data, single_ptr_block);
            if (err_code != SUCCESS) {
                free(ptr_data);
                return err_code;
            }
        }
        
        // Add block to single pointer and write
        ptr_data[index_in_single_pointer] = block_was_alloc;
        err_code = write_block(ptr_data, single_ptr_block);
        free(ptr_data);
        if (err_code != SUCCESS) {
            return err_code;
        }
    }

    // Update size and return parameter
    inode->metadata.i_blocks++;
    *returned_block_num = block_was_alloc;

    return SUCCESS;
}

int free_from_single_ptr(block_no_t single_block, struct inode_st *inode, unsigned char *block_bitmap_data, int *total_removed) {
    toggle_in_bitmap_data(block_bitmap_data, single_block);
    block_no_t *single_ptr_data = malloc(get_bytes_per_block());
    err_t err_code = read_block(single_ptr_data, single_block);
    if (err_code != SUCCESS) {
        free(single_ptr_data);
        return err_code;
    }
    for (int i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        toggle_in_bitmap_data(block_bitmap_data, single_ptr_data[i]);
        (*total_removed)++;
        if ((*total_removed) == inode->metadata.i_blocks) {
            free(single_ptr_data);
            return -1;
        }
    }

    free(single_ptr_data);
    return SUCCESS;
}

int free_from_double_ptr(block_no_t double_block, struct inode_st *inode, unsigned char *block_bitmap_data, int *total_removed) {
    toggle_in_bitmap_data(block_bitmap_data, double_block);
    block_no_t *double_ptr_data = malloc(get_bytes_per_block());
    err_t err_code = read_block(double_ptr_data, double_block);
    if (err_code != SUCCESS) {
        free(double_ptr_data);
        return err_code;
    }

    for (int i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        int ret_val = free_from_single_ptr(double_ptr_data[i], inode, block_bitmap_data, total_removed);
        if (ret_val != SUCCESS) {
            free(double_ptr_data);
            return ret_val;
        }
    }

    free(double_ptr_data);
    return SUCCESS;
}

int free_from_triple_ptr(block_no_t triple_block, struct inode_st *inode, unsigned char *block_bitmap_data, int *total_removed) {
    toggle_in_bitmap_data(block_bitmap_data, triple_block);
    block_no_t *triple_ptr_data = malloc(get_bytes_per_block());
    err_t err_code = read_block(triple_ptr_data, triple_block);
    if (err_code != SUCCESS) {
        free(triple_ptr_data);
        return err_code;
    }

    for (int i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        int ret_val = free_from_double_ptr(triple_ptr_data[i], inode, block_bitmap_data, total_removed);
        if (ret_val != SUCCESS) {
            free(triple_ptr_data);
            return ret_val;
        }
    }

    free(triple_ptr_data);
    return SUCCESS;
}

err_t clear_blocks_of_inode(struct inode_st *inode, int skip_first) {
    // Get bitmap data
    unsigned char *block_bitmap_data = malloc(get_bytes_per_block());
    int err_code = read_block(block_bitmap_data, BLOCK_BITMAP);
    if (err_code != SUCCESS) {
        return err_code;
    }

    // Remove all single blocks
    int total_removed = skip_first;
    int single_blocks = inode->metadata.i_blocks < 16 ? 15 : 0;
    for (int i = skip_first; i < single_blocks; i++) {
        toggle_in_bitmap_data(block_bitmap_data, inode->blocks[i]);
        total_removed++;
        if (total_removed == inode->metadata.i_blocks) {
            write_block(block_bitmap_data, BLOCK_BITMAP);
            free(block_bitmap_data);
            inode->metadata.i_blocks = skip_first;
            inode->metadata.i_size = 0;
            return SUCCESS;
        }
    }
    
    // Remove all blocks in single pointer from bitmap
    int ret_val = free_from_single_ptr(inode->blocks[12], inode, block_bitmap_data, &total_removed);
    if (ret_val == -1) {
        write_block(block_bitmap_data, BLOCK_BITMAP);
        free(block_bitmap_data);
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        free(block_bitmap_data);
        return ret_val;
    }

    // Remove all blocks in double pointer from bitmap
    ret_val = free_from_double_ptr(inode->blocks[13], inode, block_bitmap_data, &total_removed);
    if (ret_val == -1) {
        write_block(block_bitmap_data, BLOCK_BITMAP);
        free(block_bitmap_data);
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        free(block_bitmap_data);
        return err_code;
    }
    
    ret_val = free_from_triple_ptr(inode->blocks[14], inode, block_bitmap_data, &total_removed);
    if (ret_val == -1) {
        write_block(block_bitmap_data, BLOCK_BITMAP);
        free(block_bitmap_data);
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        free(block_bitmap_data);
        return err_code;
    }


    write_block(block_bitmap_data, BLOCK_BITMAP);
    free(block_bitmap_data);
    inode->metadata.i_blocks = skip_first;
    inode->metadata.i_size = 0;
    return SUCCESS;
}

err_t free_file_inode(struct cached_inode_st *cache_inode) {
    // Remove inode from inode bitmap
    unsigned char *inode_bitmap_data = malloc(get_bytes_per_block());
    err_t err_code = read_block(inode_bitmap_data, INODE_BITMAP);
    if (err_code != SUCCESS) {
        free(inode_bitmap_data);
        return err_code;
    }

    toggle_in_bitmap_data(inode_bitmap_data, cache_inode->id-1);
    write_block(inode_bitmap_data, INODE_BITMAP);
    free(inode_bitmap_data);

    // Get inode struct
    struct inode_st *inode = &cache_inode->inode;
    
    return clear_blocks_of_inode(inode, 0);
}

err_t add_new_file_inode(ino_id_t *inode_num, int file_type) {
    // If return params are NULL, give temp values to not cause null reference errors
    if (inode_num == NULL) {
        ino_id_t inode_temp = 0;
        inode_num = &inode_temp;
    }

    // Allocate new inode num from bitmap
    err_t err_code = find_free_inode(inode_num, 1);
    if (err_code != SUCCESS) {
        return err_code;
    }

    // Read in block containing this inode num
    block_no_t block_with_inode = (*inode_num - 1) / INODES_PER_BLOCK + INODE_TABLE_START_BLOCK;
    int inode_idx_in_block = (*inode_num - 1) % INODES_PER_BLOCK;

    struct inode_st *data = malloc(get_bytes_per_block());
    err_code = read_block(data, block_with_inode);
    if (err_code != SUCCESS) {
        free(data);
        return err_code;
    }

    // Update new inode with given data
    struct inode_st *new_node = &data[inode_idx_in_block];
    new_node->metadata.i_links_count = 0;
    new_node->metadata.i_blocks = 0;

    // Write data back
    err_code = write_block(data, block_with_inode);
    free(data);
    if (err_code != SUCCESS) {
        return err_code;
    }

    return SUCCESS;
}

err_t remove_last_block_inode(ino_id_t id) {
    struct cached_inode_st *inode_cache;
    err_t err = get_inode(&inode_cache, id);
    if (err) return err;

    inode_cache->dirty = 1;

    int block_idx = inode_cache->inode.metadata.i_blocks - 1;
    block_no_t block_to_remove = get_block_num_from_inode(&inode_cache->inode, block_idx);
    if (block_to_remove == 0) {
        return ILLEGAL_BLOCK_NO;
    }
    inode_cache->inode.metadata.i_blocks--;

    unsigned char *bitmap_data = malloc(get_bytes_per_block());
    err = read_block(bitmap_data, BLOCK_BITMAP);
    if (err) {
        free(bitmap_data);
        return err;
    }

    toggle_in_bitmap_data(bitmap_data, block_to_remove);

    if (block_idx == 16) {
        // remove single pointer and puts back into inode blocks 
        block_no_t single_ptr = inode_cache->inode.blocks[12];
        block_no_t *data = malloc(get_bytes_per_block());
        read_block(data, single_ptr);
        inode_cache->inode.blocks[14] = data[2];
        inode_cache->inode.blocks[13] = data[1];
        inode_cache->inode.blocks[12] = data[0];
        toggle_in_bitmap_data(bitmap_data, single_ptr);
        free(data);
    }
    remove_ref_from_cache(id);

    err = write_block(bitmap_data, BLOCK_BITMAP);
    free(bitmap_data);
    if (err) {
        return err;
    }
    
    return SUCCESS;
}
