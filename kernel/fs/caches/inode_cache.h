#pragma once

#include "inodes.h"
#include "memory/kmalloc.h"

struct cached_inode_st {
    struct inode_st inode; // Actual cached inode data
    ino_id_t id; // id of inode
    int dirty; // boolean if changed made to inode struct and needs to write
};

struct cache_ll_node_st {
    int num_refs; // Number of open references to this inode in cache
    struct cached_inode_st cache_node; // Data of cached inode
    struct cache_ll_node_st *next; // Linked List next element
    struct cache_ll_node_st *prev; // Linked List previous element
};

// Gets inode data from cache if open, or creates new instance in cache if not
// Increases number of references to this element by 1
struct cached_inode_st *get_inode_from_cache(ino_id_t id);

// Removes 1 reference from this element in cache
// Returns -1 if not in cache
err_t remove_ref_from_cache(ino_id_t id);

// Empties all items from inode cache and writes to disk if dirty
err_t empty_inode_cache();
