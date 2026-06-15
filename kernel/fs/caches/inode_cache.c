#include "inode_cache.h"

// Static helper functions
static int find_inode_in_cache(ino_id_t id, struct cache_ll_node_st **node);
static int remove_cache_inode(struct cache_ll_node_st *node);

// Static data structure variables
static struct cache_ll_node_st *head = NULL;
static struct cache_ll_node_st *tail = NULL;

struct cached_inode_st *get_inode_from_cache(ino_id_t id) {
    // Returns inode if already in cache and updates refs by 1
    struct cache_ll_node_st *node = NULL;
    int found = find_inode_in_cache(id, &node);
    if (found) {
        node->num_refs++;
        return &node->cache_node;
    }

    // If not in cache yet, create it
    node = kmalloc(sizeof(struct cache_ll_node_st));
    *node = (struct cache_ll_node_st) {
        .num_refs = 1,
        .next = NULL,
        .prev = tail,
    };
    
    // Add to end of linked list
    if (tail != NULL) {
        tail->next = node;
    }
    tail = node;
    if (head == NULL) {
        head = node;
    }

    // Read in inode data and add to cached node
    node->cache_node = (struct cached_inode_st) {
        .id = id,
        .dirty = 0,
    };
    err_t error = get_inode_raw(&node->cache_node.inode, id);
    if (error != SUCCESS) {
        if (node == head) {
            head = node->next;
        } else {
            node->prev->next = node->next;
        }
        if (node == tail) {
            tail = node->prev;
        } else {
            node->next->prev = node->prev;
        }
        kfree(node);
        print_error(error);
        return NULL;
    }
    
    // Return node
    return &node->cache_node;
}

err_t remove_ref_from_cache(ino_id_t id) {
    // Gets inode from cache, and returns -1 if not cached
    struct cache_ll_node_st *node = NULL;
    int found = find_inode_in_cache(id, &node);
    if (!found) {
        return FILE_NOT_FOUND;
    }

    // Reduced num_refs by one, and removed inode from cache if 0 refs
    node->num_refs--;
    if (node->num_refs <= 0) {
        remove_cache_inode(node);
    }

    return SUCCESS;
}

err_t empty_inode_cache() {
    // Iterates over all cached inodes and removes them from cache
    while (head != NULL) {
        err_t error = remove_cache_inode(head);
        if (error != SUCCESS) {
            return error;
        }
    }

    return SUCCESS;
}

static int remove_cache_inode(struct cache_ll_node_st *node) {
    // Updates prev element/head
    if (node == head) {
        head = node->next;
    } else {
        node->prev->next = node->next;
    }

    // Updates next elem/tail
    if (node == tail) {
        tail = node->prev;
    } else {
        node->next->prev = node->prev;
    }

    // If dirty, write inode to disk
    if (node->cache_node.dirty) {
        err_t error = write_inode(&node->cache_node.inode, node->cache_node.id);
        if (error != SUCCESS) {
            kfree(node);
            return error;
        }
    }

    kfree(node);
    return SUCCESS;
}

// returns boolean 1 if found, 0 if not found
static int find_inode_in_cache(ino_id_t id, struct cache_ll_node_st **node) {
    // Iterates over all nodes in Linked List and returns item if id matches
    struct cache_ll_node_st *curr = head;
    while (curr != NULL) {
        if (curr->cache_node.id == id) {
            *node = curr;
            return 1;
        }
        curr = curr->next;
    }

    return 0;
}
