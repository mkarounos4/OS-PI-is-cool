#pragma once

#include "../fat-helpers/disk.h"

#define MAX_SIZE 12

struct node_st {
    unsigned char *data; // bytes_per_block bytes of data from block
    int dirty; // boolean if data was modified
    block_no_t block; // block_no_t of this block
    
    struct node_st *next_node; // Next node in linked list
    struct node_st *prev_node; // Prev node in linked list
};

// Fetches instance of this node from cache or reads in new if doesn't exist
// and adds it to front of cache. Removes (and writes if dirty) tail if past MAX_SIZE
err_t lru_cache_add_to_front(struct node_st **ret_node, block_no_t block_num);

// Updates data of block with block_num in cache. Moves to front of lru_cache and creates new if necessary
err_t lru_cache_update_data(void *data, block_no_t block_num);

// Empties all elements from LRU cache and writes blocks if dirty
err_t lru_cache_empty();
