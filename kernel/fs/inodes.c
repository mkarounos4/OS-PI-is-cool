#include "inodes.h"

#include "disk/block.h"
#include "timer/timer.h"
#include "devices/devices.h"

#define MKFS_FILL_CHUNK_BLOCKS 32u

static void set_bitmap_bit_in_memory(unsigned char *data, uint32_t bit_idx) {
    data[bit_idx / 8u] |= (unsigned char)(1u << (7u - (bit_idx % 8u)));
}

static uint32_t get_inode_capacity(void) {
    return (uint32_t)get_num_table_blocks() * (uint32_t)INODES_PER_BLOCK;
}

static err_t set_block_allocated(block_no_t block_num, int allocated) {
    return set_bit_in_bitmap_range(get_block_bitmap_start(),
                                   get_block_bitmap_blocks(),
                                   get_total_fs_blocks(),
                                   block_num,
                                   allocated);
}

static err_t set_inode_allocated(ino_id_t inode_id, int allocated) {
    if (inode_id == 0) {
        return INVALID_ARGS;
    }
    return set_bit_in_bitmap_range(get_inode_bitmap_start(),
                                   get_inode_bitmap_blocks(),
                                   get_inode_capacity(),
                                   inode_id - 1u,
                                   allocated);
}

static err_t write_filled_blocks(block_no_t start_block,
                                 uint32_t block_count,
                                 unsigned char value,
                                 int bytes_per_block) {
    if (block_count == 0) {
        return SUCCESS;
    }

    if ((uint32_t)bytes_per_block != block_get_size()) {
        return INVALID_ARGS;
    }

    uint32_t chunk_blocks = MKFS_FILL_CHUNK_BLOCKS;
    unsigned char *data = kmalloc((uint32_t)bytes_per_block * chunk_blocks);
    if (data == NULL) {
        return FILE_WRITE_ERROR;
    }

    memset(data, value, (uint32_t)bytes_per_block * chunk_blocks);

    uint32_t remaining = block_count;
    block_no_t current = start_block;
    while (remaining > 0) {
        uint32_t chunk = remaining < chunk_blocks ? remaining : chunk_blocks;
        if (block_write(fs_get_base_block() + current, chunk, data) != 0) {
            kfree(data);
            return FILE_WRITE_ERROR;
        }
        current += chunk;
        remaining -= chunk;
    }

    kfree(data);
    return SUCCESS;
}

static err_t write_block_bitmap(int bytes_per_block) {
    unsigned char *data = kmalloc(bytes_per_block);
    if (data == NULL) {
        return FILE_WRITE_ERROR;
    }

    uint32_t bits_per_block = (uint32_t)bytes_per_block * 8u;
    uint64_t used_blocks = (uint64_t)get_data_start_block() + 1u;
    uint64_t total_blocks = get_total_fs_blocks();
    uint32_t bitmap_blocks = get_block_bitmap_blocks();
    uint32_t block = 0;

    while (block < bitmap_blocks) {
        uint64_t first_bit = (uint64_t)block * bits_per_block;
        uint64_t end_bit = first_bit + bits_per_block;

        if (end_bit <= used_blocks || first_bit >= total_blocks) {
            uint32_t run = 1;
            while (block + run < bitmap_blocks) {
                uint64_t run_first = (uint64_t)(block + run) * bits_per_block;
                uint64_t run_end = run_first + bits_per_block;
                if (!(run_end <= used_blocks || run_first >= total_blocks)) {
                    break;
                }
                run++;
            }
            err_t err = write_filled_blocks(get_block_bitmap_start() + block,
                                            run, 0xFF, bytes_per_block);
            if (err != SUCCESS) {
                kfree(data);
                return err;
            }
            block += run;
            continue;
        }

        if (first_bit >= used_blocks && end_bit <= total_blocks) {
            uint32_t run = 1;
            while (block + run < bitmap_blocks) {
                uint64_t run_first = (uint64_t)(block + run) * bits_per_block;
                uint64_t run_end = run_first + bits_per_block;
                if (!(run_first >= used_blocks && run_end <= total_blocks)) {
                    break;
                }
                run++;
            }
            err_t err = write_filled_blocks(get_block_bitmap_start() + block,
                                            run, 0x00, bytes_per_block);
            if (err != SUCCESS) {
                kfree(data);
                return err;
            }
            block += run;
            continue;
        }

        memset(data, 0, bytes_per_block);
        for (uint32_t bit = 0; bit < bits_per_block; bit++) {
            uint64_t global_bit = first_bit + bit;
            if (global_bit < used_blocks || global_bit >= total_blocks) {
                set_bitmap_bit_in_memory(data, bit);
            }
        }

        err_t err = write_block(data, get_block_bitmap_start() + block);
        if (err != SUCCESS) {
            kfree(data);
            return err;
        }
        block++;
    }

    kfree(data);
    return SUCCESS;
}

static err_t write_inode_bitmap(int bytes_per_block) {
    unsigned char *data = kmalloc(bytes_per_block);
    if (data == NULL) {
        return FILE_WRITE_ERROR;
    }

    uint32_t bits_per_block = (uint32_t)bytes_per_block * 8u;
    uint32_t inode_count = get_inode_capacity();
    for (uint32_t block = 0; block < get_inode_bitmap_blocks(); block++) {
        memset(data, 0, bytes_per_block);
        uint32_t first_bit = block * bits_per_block;
        for (uint32_t bit = 0; bit < bits_per_block; bit++) {
            uint32_t global_bit = first_bit + bit;
            if (global_bit == 0 || global_bit >= inode_count) {
                set_bitmap_bit_in_memory(data, bit);
            }
        }
        err_t err = write_block(data, get_inode_bitmap_start() + block);
        if (err != SUCCESS) {
            kfree(data);
            return err;
        }
    }

    kfree(data);
    return SUCCESS;
}

err_t mkfs_inode(int inode_table_blocks, int bytes_per_block) {
    unsigned char *data = kmalloc(bytes_per_block);
    if (data == NULL) {
        return FILE_WRITE_ERROR;
    }
    memset(data, 0, bytes_per_block);

    struct superblock_st super;
    memset(&super, 0, sizeof(super));
    memcpy(super.signature, FS_SIGNATURE, FS_SIGNATURE_SIZE);
    super.bytes_per_block = (uint32_t)bytes_per_block;
    super.total_blocks = get_total_fs_blocks();
    super.block_bitmap_start = get_block_bitmap_start();
    super.block_bitmap_blocks = get_block_bitmap_blocks();
    super.inode_bitmap_start = get_inode_bitmap_start();
    super.inode_bitmap_blocks = get_inode_bitmap_blocks();
    super.inode_table_start = get_inode_table_start();
    super.inode_table_blocks = (uint32_t)inode_table_blocks;
    super.data_start_block = get_data_start_block();
    super.root_inode_id = ROOT_INO;

    memset(data, 0, bytes_per_block);
    err_t err = write_block(data, SUPERBLOCK_BLOCK);
    if (err != SUCCESS) {
        kfree(data);
        return err;
    }

    err = write_block_bitmap(bytes_per_block);
    if (err != SUCCESS) {
        kfree(data);
        return err;
    }

    err = write_inode_bitmap(bytes_per_block);
    if (err != SUCCESS) {
        kfree(data);
        return err;
    }

    // Initialize inode table and root directory inode.
    struct inode_st *data_inodes = kmalloc(bytes_per_block);
    if (data_inodes == NULL) {
        kfree(data);
        return FILE_WRITE_ERROR;
    }
    memset(data_inodes, 0, bytes_per_block);
    struct inode_st root_node;
    memset(&root_node, 0, sizeof(root_node));
    attributes_t meta = {
        .i_links_count = 2,
        .type = DIRECTORY_TYPE,
        .perm = 0x7,
        .i_blocks = 1,
        .mtime = timer_get_ticks(),
    };
    root_node.metadata = meta;
    root_node.blocks[0] = get_data_start_block();
    data_inodes[0] = root_node;

    err = write_block(data_inodes, get_inode_table_start());
    if (err != SUCCESS) {
        kfree(data_inodes);
        kfree(data);
        return err;
    }

    if (inode_table_blocks > 1) {
        err = write_filled_blocks(get_inode_table_start() + 1u,
                                  (uint32_t)inode_table_blocks - 1u,
                                  0x00, bytes_per_block);
        if (err != SUCCESS) {
            kfree(data_inodes);
            kfree(data);
            return err;
        }
    }

    // add dirent root
    struct fs_dirent* data_root = kmalloc(bytes_per_block);
    if (data_root == NULL) {
        kfree(data_inodes);
        kfree(data);
        return FILE_WRITE_ERROR;
    }
    memset(data_root, 0, bytes_per_block);
    data_root[0] = (struct fs_dirent) {
        .name =  ".",
        .ino_id = 1,
    };
    data_root[1] = (struct fs_dirent) {
        .name =  "..",
        .ino_id = 1,
    };
    data_root[2] = (struct fs_dirent) {
        .name = "\0",
    };

    err = write_block(data_root, get_data_start_block());
    kfree(data_root);
    if (err != SUCCESS) {
        kfree(data_inodes);
        kfree(data);
        return err;
    }

    memset(data, 0, bytes_per_block);
    memcpy(data, &super, sizeof(super));
    err = write_block(data, SUPERBLOCK_BLOCK);
    if (err != SUCCESS) {
        kfree(data_inodes);
        kfree(data);
        return err;
    }

    kfree(data);
    kfree(data_inodes);

    return SUCCESS;
}

err_t mount_inode(const struct superblock_st *superblock,
                  int *bytes_per_block,
                  int *num_inode_blocks) {
    if (superblock == NULL || bytes_per_block == NULL ||
        num_inode_blocks == NULL) {
        return INVALID_ARGS;
    }

    err_t err = fs_set_layout(superblock->bytes_per_block,
                              superblock->total_blocks,
                              superblock->block_bitmap_start,
                              superblock->block_bitmap_blocks,
                              superblock->inode_bitmap_start,
                              superblock->inode_bitmap_blocks,
                              superblock->inode_table_start,
                              superblock->inode_table_blocks,
                              superblock->data_start_block);
    if (err != SUCCESS) {
        return err;
    }

    *bytes_per_block = (int)superblock->bytes_per_block;
    *num_inode_blocks = (int)superblock->inode_table_blocks;

    return SUCCESS;
}

err_t unmount_inode() {
    empty_inode_cache();

    return SUCCESS;
}

err_t get_inode_raw(struct inode_st *node, ino_id_t id) {
    block_no_t block_with_inode =
        (id - 1) / INODES_PER_BLOCK + get_inode_table_start();

    int inode_num_in_block =
        (id - 1) % INODES_PER_BLOCK;
    void *data = kmalloc(get_bytes_per_block());
    int err = read_block(data, block_with_inode);
    if (err != 0) {
        kfree(data);
        return err;
    }

    struct inode_st *inodes = (struct inode_st*) data;
    *node = inodes[inode_num_in_block];
    
    kfree(data);
    return SUCCESS;
}

err_t get_inode(struct cached_inode_st** node, ino_id_t id) {
    *node = get_inode_from_cache(id);
    return *node == NULL ? FILE_READ_ERROR : SUCCESS;
}

err_t get_inode_metadata(ino_id_t id, attributes_t *metadata) {
    if (metadata == NULL || id == 0) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *node;
    err_t err = get_inode(&node, id);
    if (err != SUCCESS) {
        return err;
    }

    *metadata = node->inode.metadata;
    remove_ref_from_cache(id);
    return SUCCESS;
}

err_t update_inode_metadata(ino_id_t id, int flags, uint8_t type, uint8_t perm) {
    if (id == 0) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *node;
    err_t err = get_inode(&node, id);
    if (err != SUCCESS) {
        return err;
    }

    if (flags & INODE_EDIT_TYPE) {
        node->inode.metadata.type = type;
    }
    if (flags & INODE_EDIT_PERM) {
        if (flags & INODE_AND_PERM) {
            node->inode.metadata.perm &= perm;
        } else {
            node->inode.metadata.perm |= perm;
        }
    }
    if (flags & INODE_EDIT_MTIME) {
        node->inode.metadata.mtime = timer_get_ticks();
    }

    node->dirty = 1;
    remove_ref_from_cache(id);
    return SUCCESS;
}

err_t set_inode_metadata(ino_id_t id, attributes_t *metadata) {
    if (id == 0) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *node;
    err_t err = get_inode(&node, id);
    if (err) {
        return err;
    }

    node->dirty = 1;
    node->inode.metadata = *metadata;
    remove_ref_from_cache(id);
    return SUCCESS;
}

err_t write_inode(struct inode_st *node, ino_id_t id) {
    block_no_t block_with_inode =
        (id - 1) / INODES_PER_BLOCK + get_inode_table_start();

    int inode_num_in_block =
        (id - 1) % INODES_PER_BLOCK;
    struct inode_st *data = kmalloc(get_bytes_per_block());
    int err = read_block(data, block_with_inode);
    if (err != 0) {
        kfree(data);
        return err;
    }

    data[inode_num_in_block] = *node;
    err = write_block(data, block_with_inode);
    if (err != SUCCESS) {
        kfree(data);
        return err;
    }

    kfree(data);
    return SUCCESS;
}

err_t find_free_block(block_no_t *free_block, int update_taken) {
    uint32_t free_block_u = 0;
    err_t err_code = find_free_from_bitmap_range(&free_block_u,
                                                 get_block_bitmap_start(),
                                                 get_block_bitmap_blocks(),
                                                 get_total_fs_blocks(),
                                                 update_taken);
    if (err_code != SUCCESS) {
        return err_code;
    }
    *free_block = (block_no_t)free_block_u;
    return SUCCESS;
}

err_t find_free_inode(ino_id_t *free_block, int update_taken) {
    uint32_t free_block_u = 0;
    uint32_t inode_capacity = get_inode_capacity();
    err_t err_code = find_free_from_bitmap_range(&free_block_u,
                                                 get_inode_bitmap_start(),
                                                 get_inode_bitmap_blocks(),
                                                 inode_capacity,
                                                 update_taken);
    if (err_code != SUCCESS) {
        return err_code;
    }
    *free_block = (ino_id_t)(free_block_u + 1u);
    return SUCCESS;
}

block_no_t get_block_num_from_disk_ptr(block_no_t ptr_block_num, unsigned int desired_block_idx) {
    void *data = kmalloc(get_bytes_per_block());
    read_block(data, ptr_block_num);
    block_no_t *blocks_ptr_arr = (block_no_t*) data;
    block_no_t to_return = blocks_ptr_arr[desired_block_idx];
    kfree(data);
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
        block_no_t *ptr_data = kmalloc(get_bytes_per_block());
        
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
                kfree(ptr_data);
                return err_code;
            }
        }

        // Add block to single pointer
        ptr_data[inode->metadata.i_blocks - 12] = block_was_alloc;
        err_code = write_block(ptr_data, inode->blocks[12]);
        kfree(ptr_data);
        if (err_code != SUCCESS) {
            return err_code;
        }
    } else if (inode->metadata.i_blocks < 12 + BLOCKS_IN_SINGLE_PTR + BLOCKS_IN_DOUBLE_PTR) {
        // Handle double pointer case
        unsigned int num_in_double_ptr = inode->metadata.i_blocks - 12 - BLOCKS_IN_SINGLE_PTR;
        unsigned int index_in_single_ptr = num_in_double_ptr % BLOCKS_IN_SINGLE_PTR;
        unsigned int single_ptr_idx_in_double_ptr = num_in_double_ptr / BLOCKS_IN_SINGLE_PTR;
        
        block_no_t *ptr_data = kmalloc(get_bytes_per_block());
        block_no_t single_ptr_block;
        
        // Create new single or double block if necessary
        if (index_in_single_ptr == 0) {
            err_code = find_free_block(&single_ptr_block, 1);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
                return err_code;
            }
            
            block_no_t *double_ptr_data = kmalloc(get_bytes_per_block());
            if (single_ptr_idx_in_double_ptr == 0) {
                err_code = find_free_block(&inode->blocks[13], 1);
                if (err_code != SUCCESS) {
                    kfree(ptr_data);
                    kfree(double_ptr_data);
                    return err_code;
                }
            } else {
                err_code = read_block(double_ptr_data, inode->blocks[13]);
                if (err_code != SUCCESS) {
                    kfree(ptr_data);
                    kfree(double_ptr_data);
                    return err_code;
                }
            }

            double_ptr_data[single_ptr_idx_in_double_ptr] = single_ptr_block;
            err_code = write_block(double_ptr_data, inode->blocks[13]);
            kfree(double_ptr_data);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
                return err_code;
            }
        } else {
            block_no_t *double_ptr_data = kmalloc(get_bytes_per_block());
            err_code = read_block(double_ptr_data, inode->blocks[13]);
            if (err_code != SUCCESS) {
                kfree(double_ptr_data);
                kfree(ptr_data);
                return err_code;
            }

            single_ptr_block = double_ptr_data[single_ptr_idx_in_double_ptr];
            err_code = read_block(ptr_data, single_ptr_block);
            kfree(double_ptr_data);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
                return err_code;
            }
        }

        ptr_data[index_in_single_ptr] = block_was_alloc;
        err_code = write_block(ptr_data, single_ptr_block);
        kfree(ptr_data);
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

        block_no_t *ptr_data = kmalloc(get_bytes_per_block());
        block_no_t single_ptr_block; 

        // Create new triple or double or single block if necessary
        if (index_in_single_pointer == 0) {
            // new single pointer
            err_code = find_free_block(&single_ptr_block, 1);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
                return err_code;
            }

            block_no_t *double_ptr_data = kmalloc(get_bytes_per_block());
            block_no_t double_ptr_block;
            if (single_ptr_idx_in_double_ptr == 0) {
                // New double pointer
                err_code = find_free_block(&double_ptr_block, 1);
                if (err_code != SUCCESS) {
                    kfree(ptr_data);
                    kfree(double_ptr_data);
                    return err_code;
                }

                block_no_t *triple_ptr_data = kmalloc(get_bytes_per_block());
                if (double_ptr_idx_in_triple_ptr == 0) {
                    // New triple pointer
                    err_code = find_free_block(&inode->blocks[14], 1);
                    if (err_code != SUCCESS) {
                        kfree(triple_ptr_data);
                        kfree(double_ptr_data);
                        kfree(ptr_data);
                        return err_code;
                    }
                } else {
                    // Old triple pointer, to place new double pointer
                    err_code = read_block(triple_ptr_data, inode->blocks[14]);
                    if (err_code != SUCCESS) {
                        kfree(triple_ptr_data);
                        kfree(double_ptr_data);
                        kfree(ptr_data);
                        return err_code;
                    }
                }

                // Add double pointer to triple pointer block and write
                triple_ptr_data[double_ptr_idx_in_triple_ptr] = double_ptr_block;
                err_code = write_block(triple_ptr_data, inode->blocks[14]);
                kfree(triple_ptr_data);
                if (err_code != SUCCESS) {
                    kfree(double_ptr_data);
                    kfree(ptr_data);
                    return err_code;
                }
            } else {
                // Read data from existing double pointer block
                block_no_t *triple_ptr_data = kmalloc(get_bytes_per_block());
                err_code = read_block(triple_ptr_data, inode->blocks[14]);
                if (err_code != SUCCESS) {
                    kfree(triple_ptr_data);
                    kfree(double_ptr_data);
                    kfree(ptr_data);
                    return err_code;
                }

                double_ptr_block = triple_ptr_data[double_ptr_idx_in_triple_ptr];
                err_code = read_block(double_ptr_data, double_ptr_block);
                kfree(triple_ptr_data);
                if (err_code != SUCCESS) {
                    kfree(double_ptr_data);
                    kfree(ptr_data);
                    return err_code;
                }
            }

            double_ptr_data[single_ptr_idx_in_double_ptr] = single_ptr_block;
            err_code = write_block(double_ptr_data, double_ptr_block);
            kfree(double_ptr_data);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
            }
        } else {
            // Read from existing single ptr block
            block_no_t *triple_ptr_data = kmalloc(get_bytes_per_block());
            err_code = read_block(triple_ptr_data, inode->blocks[14]);
            if (err_code != SUCCESS) {
                kfree(triple_ptr_data);
                kfree(ptr_data);
                return err_code;
            }

            block_no_t double_ptr_block = triple_ptr_data[double_ptr_idx_in_triple_ptr];
            kfree(triple_ptr_data);

            
            block_no_t *double_ptr_data = kmalloc(get_bytes_per_block());
            err_code = read_block(double_ptr_data, double_ptr_block);
            if (err_code != SUCCESS) {
                kfree(double_ptr_data);
                kfree(ptr_data);
                return err_code;
            }

            single_ptr_block = double_ptr_data[single_ptr_idx_in_double_ptr];
            kfree(double_ptr_data);

            err_code = read_block(ptr_data, single_ptr_block);
            if (err_code != SUCCESS) {
                kfree(ptr_data);
                return err_code;
            }
        }
        
        // Add block to single pointer and write
        ptr_data[index_in_single_pointer] = block_was_alloc;
        err_code = write_block(ptr_data, single_ptr_block);
        kfree(ptr_data);
        if (err_code != SUCCESS) {
            return err_code;
        }
    }

    // Update size and return parameter
    inode->metadata.i_blocks++;
    *returned_block_num = block_was_alloc;

    return SUCCESS;
}

int free_from_single_ptr(block_no_t single_block, struct inode_st *inode, int *total_removed) {
    err_t err_code = set_block_allocated(single_block, 0);
    if (err_code != SUCCESS) {
        return err_code;
    }
    block_no_t *single_ptr_data = kmalloc(get_bytes_per_block());
    err_code = read_block(single_ptr_data, single_block);
    if (err_code != SUCCESS) {
        kfree(single_ptr_data);
        return err_code;
    }
    for (size_t i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        err_code = set_block_allocated(single_ptr_data[i], 0);
        if (err_code != SUCCESS) {
            kfree(single_ptr_data);
            return err_code;
        }
        (*total_removed)++;
        if ((uint32_t)(*total_removed) == inode->metadata.i_blocks) {
            kfree(single_ptr_data);
            return -1;
        }
    }

    kfree(single_ptr_data);
    return SUCCESS;
}

int free_from_double_ptr(block_no_t double_block, struct inode_st *inode, int *total_removed) {
    err_t err_code = set_block_allocated(double_block, 0);
    if (err_code != SUCCESS) {
        return err_code;
    }
    block_no_t *double_ptr_data = kmalloc(get_bytes_per_block());
    err_code = read_block(double_ptr_data, double_block);
    if (err_code != SUCCESS) {
        kfree(double_ptr_data);
        return err_code;
    }

    for (size_t i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        int ret_val = free_from_single_ptr(double_ptr_data[i], inode, total_removed);
        if (ret_val != SUCCESS) {
            kfree(double_ptr_data);
            return ret_val;
        }
    }

    kfree(double_ptr_data);
    return SUCCESS;
}

int free_from_triple_ptr(block_no_t triple_block, struct inode_st *inode, int *total_removed) {
    err_t err_code = set_block_allocated(triple_block, 0);
    if (err_code != SUCCESS) {
        return err_code;
    }
    block_no_t *triple_ptr_data = kmalloc(get_bytes_per_block());
    err_code = read_block(triple_ptr_data, triple_block);
    if (err_code != SUCCESS) {
        kfree(triple_ptr_data);
        return err_code;
    }

    for (size_t i = 0; i < BLOCKS_IN_SINGLE_PTR; i++) {
        int ret_val = free_from_double_ptr(triple_ptr_data[i], inode, total_removed);
        if (ret_val != SUCCESS) {
            kfree(triple_ptr_data);
            return ret_val;
        }
    }

    kfree(triple_ptr_data);
    return SUCCESS;
}

err_t clear_blocks_of_inode(struct inode_st *inode, int skip_first) {
    // Remove all single blocks
    int total_removed = skip_first;
    int total_blocks = (int)inode->metadata.i_blocks;
    if (total_removed >= total_blocks) {
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    }

    int direct_blocks = total_blocks < 16 ? total_blocks : 12;
    for (int i = skip_first; i < direct_blocks; i++) {
        err_t err_code = set_block_allocated(inode->blocks[i], 0);
        if (err_code != SUCCESS) {
            return err_code;
        }
        total_removed++;
        if (total_removed == total_blocks) {
            inode->metadata.i_blocks = skip_first;
            inode->metadata.i_size = 0;
            return SUCCESS;
        }
    }
    
    // Remove all blocks in single pointer from bitmap
    int ret_val = free_from_single_ptr(inode->blocks[12], inode, &total_removed);
    if (ret_val == -1) {
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        return ret_val;
    }

    // Remove all blocks in double pointer from bitmap
    ret_val = free_from_double_ptr(inode->blocks[13], inode, &total_removed);
    if (ret_val == -1) {
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        return ret_val;
    }
    
    ret_val = free_from_triple_ptr(inode->blocks[14], inode, &total_removed);
    if (ret_val == -1) {
        inode->metadata.i_blocks = skip_first;
        inode->metadata.i_size = 0;
        return SUCCESS;
    } else if (ret_val != SUCCESS) {
        return ret_val;
    }

    inode->metadata.i_blocks = skip_first;
    inode->metadata.i_size = 0;
    return SUCCESS;
}

err_t free_file_inode(struct cached_inode_st *cache_inode) {
    // Remove inode from inode bitmap
    err_t err_code = set_inode_allocated(cache_inode->id, 0);
    if (err_code != SUCCESS) {
        return err_code;
    }

    // Get inode struct
    struct inode_st *inode = &cache_inode->inode;
    
    return clear_blocks_of_inode(inode, 0);
}

err_t add_new_file_inode(ino_id_t *inode_num, int file_type, uint8_t perm, struct file_operations *fops) {
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
    block_no_t block_with_inode = (*inode_num - 1) / INODES_PER_BLOCK + get_inode_table_start();
    int inode_idx_in_block = (*inode_num - 1) % INODES_PER_BLOCK;

    struct inode_st *data = kmalloc(get_bytes_per_block());
    err_code = read_block(data, block_with_inode);
    if (err_code != SUCCESS) {
        kfree(data);
        return err_code;
    }

    // Update new inode with given data
    struct inode_st *new_node = &data[inode_idx_in_block];
    new_node->metadata.i_links_count = 0;
    new_node->metadata.type = file_type;
    new_node->metadata.perm = perm;
    new_node->metadata.i_blocks = 0;
    new_node->metadata.mtime = timer_get_ticks();
    new_node->metadata.fops = fops;
    new_node->metadata.i_pipe = NULL;

    // Write data back
    err_code = write_block(data, block_with_inode);
    kfree(data);
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

    err = set_block_allocated(block_to_remove, 0);
    if (err) return err;

    if (block_idx == 16) {
        // remove single pointer and puts back into inode blocks 
        block_no_t single_ptr = inode_cache->inode.blocks[12];
        block_no_t *data = kmalloc(get_bytes_per_block());
        read_block(data, single_ptr);
        inode_cache->inode.blocks[14] = data[2];
        inode_cache->inode.blocks[13] = data[1];
        inode_cache->inode.blocks[12] = data[0];
        err = set_block_allocated(single_ptr, 0);
        if (err) {
            kfree(data);
            return err;
        }
        kfree(data);
    }
    remove_ref_from_cache(id);
    
    return SUCCESS;
}
