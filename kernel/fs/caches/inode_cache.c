#include "inode_cache.h"

#include "sync/spinlock.h"

// Static helper functions
static int find_inode_in_cache(ino_id_t id, struct cache_ll_node_st **node);
static void unlink_cache_inode_locked(struct cache_ll_node_st *node);
static err_t flush_and_free_cache_inode(struct cache_ll_node_st *node);

// Static data structure variables
static struct cache_ll_node_st *head = NULL;
static struct cache_ll_node_st *tail = NULL;
static uint32_t inode_cache_hits = 0;
static uint32_t inode_cache_misses = 0;
static uint32_t inode_cache_evictions = 0;
static spinlock_t inode_cache_lock = SPINLOCK_INIT;

struct cached_inode_st *get_inode_from_cache(ino_id_t id) {
    struct cache_ll_node_st *new_node = kmalloc(sizeof(struct cache_ll_node_st));
    if (new_node == NULL) {
        return NULL;
    }
    *new_node = (struct cache_ll_node_st) {
        .num_refs = 1,
        .loading = 1,
        .load_error = SUCCESS,
        .next = NULL,
        .prev = NULL,
    };
    new_node->cache_node = (struct cached_inode_st) {
        .id = id,
        .dirty = 0,
        .lock = SPINLOCK_INIT,
    };

    uint64_t flags = spin_lock_irqsave(&inode_cache_lock);
    struct cache_ll_node_st *node = NULL;
    int found = find_inode_in_cache(id, &node);
    if (found) {
        inode_cache_hits++;
        node->num_refs++;
        spin_unlock_irqrestore(&inode_cache_lock, flags);
        kfree(new_node);

        while (node->loading) {
            asm volatile("wfe" ::: "memory");
        }

        if (node->load_error != SUCCESS) {
            remove_ref_from_cache(id);
            return NULL;
        }

        return &node->cache_node;
    }
    inode_cache_misses++;

    node = new_node;
    node->prev = tail;
    if (tail != NULL) {
        tail->next = node;
    }
    tail = node;
    if (head == NULL) {
        head = node;
    }

    spin_unlock_irqrestore(&inode_cache_lock, flags);

    err_t error = get_inode_raw(&node->cache_node.inode, id);

    flags = spin_lock_irqsave(&inode_cache_lock);
    node->load_error = error;
    node->loading = 0;
    if (error != SUCCESS) {
        node->num_refs--;
        int should_free = node->num_refs <= 0;
        if (should_free) {
            unlink_cache_inode_locked(node);
        }
        spin_unlock_irqrestore(&inode_cache_lock, flags);
        asm volatile("dmb sy\nsev" ::: "memory");
        print_error(error);
        if (should_free) {
            kfree(node);
        }
        return NULL;
    }

    spin_unlock_irqrestore(&inode_cache_lock, flags);
    asm volatile("dmb sy\nsev" ::: "memory");
    return &node->cache_node;
}

err_t remove_ref_from_cache(ino_id_t id) {
    uint64_t flags = spin_lock_irqsave(&inode_cache_lock);
    // Gets inode from cache, and returns -1 if not cached
    struct cache_ll_node_st *node = NULL;
    int found = find_inode_in_cache(id, &node);
    if (!found) {
        spin_unlock_irqrestore(&inode_cache_lock, flags);
        return FILE_NOT_FOUND;
    }

    node->num_refs--;
    int should_free = node->num_refs <= 0 && !node->loading;
    if (should_free) {
        unlink_cache_inode_locked(node);
    }

    spin_unlock_irqrestore(&inode_cache_lock, flags);
    return should_free ? flush_and_free_cache_inode(node) : SUCCESS;
}

err_t empty_inode_cache() {
    while (1) {
        uint64_t flags = spin_lock_irqsave(&inode_cache_lock);
        struct cache_ll_node_st *node = head;
        if (node == NULL) {
            spin_unlock_irqrestore(&inode_cache_lock, flags);
            return SUCCESS;
        }
        unlink_cache_inode_locked(node);
        spin_unlock_irqrestore(&inode_cache_lock, flags);

        while (node->loading) {
            asm volatile("wfe" ::: "memory");
        }

        err_t error = flush_and_free_cache_inode(node);
        if (error != SUCCESS) {
            return error;
        }
    }
}

static void unlink_cache_inode_locked(struct cache_ll_node_st *node) {
    inode_cache_evictions++;

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

    node->next = NULL;
    node->prev = NULL;
}

static err_t flush_and_free_cache_inode(struct cache_ll_node_st *node) {
    err_t error = SUCCESS;
    uint8_t dirty;
    struct inode_st inode;

    uint64_t flags = cached_inode_lock(&node->cache_node);
    dirty = node->cache_node.dirty;
    if (dirty) {
        inode = node->cache_node.inode;
        node->cache_node.dirty = 0;
    }
    cached_inode_unlock(&node->cache_node, flags);

    if (dirty) {
        error = write_inode(&inode, node->cache_node.id);
    }
    kfree(node);
    return error;
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

void inode_cache_get_stats(struct inode_cache_stats *stats) {
    if (stats == NULL) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&inode_cache_lock);
    uint32_t used = 0;
    uint32_t dirty = 0;
    struct cache_ll_node_st *curr = head;
    while (curr != NULL) {
        used++;
        if (curr->cache_node.dirty || curr->loading) {
            dirty++;
        }
        curr = curr->next;
    }

    stats->capacity = 0;
    stats->used = used;
    stats->hits = inode_cache_hits;
    stats->misses = inode_cache_misses;
    stats->evictions = inode_cache_evictions;
    stats->dirty = dirty;
    spin_unlock_irqrestore(&inode_cache_lock, flags);
}
