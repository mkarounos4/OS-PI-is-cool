#include "lru_cache.h"

#include "disk/block.h"

// Linked list static variables
static struct node_st *head = NULL;
static struct node_st *tail = NULL;
static int size = 0;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static uint32_t cache_evictions = 0;

// Linked list static helper functions
static void lru_cache_remove_node(struct node_st *node);
static struct node_st *lru_cache_get_block(block_no_t block_num);
static err_t lru_cache_remove_back();

static err_t write_block_data(void *data, block_no_t num);
static err_t read_block_data(void *data, block_no_t num);
static err_t map_fs_block_to_disk_block(block_no_t num, uint64_t *disk_block);

static err_t map_fs_block_to_disk_block(block_no_t num, uint64_t *disk_block) {
    uint64_t fs_block_count = fs_get_block_count();
    if (fs_block_count == 0 || (uint64_t)num >= fs_block_count) {
        return INVALID_ARGS;
    }

    *disk_block = fs_get_base_block() + num;
    return SUCCESS;
}

static err_t read_block_data(void *data, block_no_t num) {
    uint64_t disk_block;
    err_t err = map_fs_block_to_disk_block(num, &disk_block);
    if (err != SUCCESS) {
        return FILE_READ_ERROR;
    }

    return block_read(disk_block, 1, data) == 0 ? SUCCESS : FILE_READ_ERROR;
}

static err_t write_block_data(void *data, block_no_t num) {
    uint64_t disk_block;
    err_t err = map_fs_block_to_disk_block(num, &disk_block);
    if (err != SUCCESS) {
        return FILE_WRITE_ERROR;
    }

    return block_write(disk_block, 1, data) == 0 ? SUCCESS : FILE_WRITE_ERROR;
}

static void lru_cache_remove_node(struct node_st *node) {
    // Update head if removing head
    if (node == head) {
        head = node->next_node;
    }

    // Update tail if removing tail
    if (node == tail) {
        tail = node->prev_node;
    }

    // Update next->prev if applicable
    if (node->next_node != NULL) {
        node->next_node->prev_node = node->prev_node;
    }

    // Update prev->next if applicable
    if (node->prev_node != NULL) {
        node->prev_node->next_node = node->next_node;
    }

    // Remove next and prev pointers
    node->next_node = NULL;
    node->prev_node = NULL;

    // update size
    size--;
}

err_t lru_cache_add_to_front(struct node_st **ret_node, block_no_t block_num) {
    // Get block from cache if already there
    struct node_st *to_add = lru_cache_get_block(block_num);

    if (to_add != NULL) {
        cache_hits++;
        // If so, remove it from current position
        lru_cache_remove_node(to_add);
    } else {
        cache_misses++;
        // If not, read in block data and create new Linked List node
        to_add = kmalloc(sizeof(struct node_st));
        *to_add = (struct node_st) {
            .data = kmalloc(get_bytes_per_block()),
            .dirty = 0,
            .block = block_num,
            .next_node = NULL,
            .prev_node = NULL
        };

        // Read in the block data to cache'd node
        err_t err_code = read_block_data(to_add->data, block_num);
        if (err_code) {
            kfree(to_add->data);
            kfree(to_add);
            return err_code;
        }
    }

    // Update size, head and tail (if necessary)
    to_add->next_node = head;
    to_add->prev_node = NULL;
    if (head == NULL) {
        tail = to_add;
    } else {
        head->prev_node = to_add;
    }
    head = to_add;
    size++;

    // Remove tail if past MAX_SIZE nodes
    if (size > MAX_SIZE) {
        err_t err_code = lru_cache_remove_back();
        if (err_code) {
            return err_code;
        }
    }

    // Update return parameter if there
    if (ret_node != NULL) {
        *ret_node = to_add;
    }

    return SUCCESS;
}

static struct node_st *lru_cache_get_block(block_no_t block_num) {
    // Iterate over all blocks in cache and return if block_num matches
    struct node_st *curr = head;
    for (int i = 0; i < size; i++) {
        if (curr->block == block_num) {
            return curr;
        }
        curr = curr->next_node;
    }

    // Return NULL if not in cache
    return NULL;
}

static err_t lru_cache_remove_back() {
    // Remove node from Linked List
    struct node_st *to_remove = tail;
    lru_cache_remove_node(to_remove);
    cache_evictions++;

    // Write data if dirty
    if (to_remove->dirty) {
        err_t err_code = write_block_data(to_remove->data, to_remove->block);
        if (err_code) {
            kfree(to_remove->data);
            kfree(to_remove);
            return err_code;
        }
    }

    // Free from memory
    kfree(to_remove->data);
    kfree(to_remove);

    return SUCCESS;
}

err_t lru_cache_update_data(void *data, block_no_t block_num) {
    struct node_st *to_update = lru_cache_get_block(block_num);

    if (to_update != NULL) {
        cache_hits++;
        lru_cache_remove_node(to_update);
    } else {
        cache_misses++;
        to_update = kmalloc(sizeof(struct node_st));
        if (to_update == NULL) {
            return FILE_WRITE_ERROR;
        }

        unsigned char *node_data = kmalloc(get_bytes_per_block());
        if (node_data == NULL) {
            kfree(to_update);
            return FILE_WRITE_ERROR;
        }

        *to_update = (struct node_st) {
            .data = node_data,
            .dirty = 0,
            .block = block_num,
            .next_node = NULL,
            .prev_node = NULL
        };
    }

    memcpy(to_update->data, data, get_bytes_per_block());

    err_t err_code = write_block_data(to_update->data, block_num);
    if (err_code != SUCCESS) {
        kfree(to_update->data);
        kfree(to_update);
        return err_code;
    }

    to_update->dirty = 0;

    to_update->next_node = head;
    to_update->prev_node = NULL;
    if (head == NULL) {
        tail = to_update;
    } else {
        head->prev_node = to_update;
    }
    head = to_update;
    size++;

    if (size > MAX_SIZE) {
        err_code = lru_cache_remove_back();
        if (err_code != SUCCESS) {
            return err_code;
        }
    }

    return SUCCESS;
}

err_t lru_cache_empty() {
    // Remove all elements from back until empty
    while (tail != NULL) {
        err_t err_code = lru_cache_remove_back();
        if (err_code) {
            return err_code;
        }
    }

    return SUCCESS;
}

void lru_cache_get_stats(struct lru_cache_stats *stats) {
    if (stats == NULL) {
        return;
    }

    uint32_t dirty = 0;
    struct node_st *curr = head;
    while (curr != NULL) {
        if (curr->dirty) {
            dirty++;
        }
        curr = curr->next_node;
    }

    stats->capacity_blocks = MAX_SIZE;
    stats->used_blocks = (uint32_t)size;
    stats->hits = cache_hits;
    stats->misses = cache_misses;
    stats->evictions = cache_evictions;
    stats->dirty_blocks = dirty;
}
