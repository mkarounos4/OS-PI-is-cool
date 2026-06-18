#include "data-structs/hashmap.h"

#include "memory/kmalloc.h"
#include "traps/traps.h"

static size_t hashmap_strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

static void hashmap_memcpy(void *dst, const void *src, size_t size) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static int hashmap_memeq(const void *lhs, const void *rhs, size_t size) {
    const unsigned char *l = lhs;
    const unsigned char *r = rhs;
    for (size_t i = 0; i < size; i++) {
        if (l[i] != r[i]) {
            return 0;
        }
    }
    return 1;
}

static size_t hashmap_key_size(hashmap_key_t key) {
    if (key.type == HASHMAP_KEY_STRING) {
        return hashmap_strlen((const char *)key.data) + 1;
    }
    return key.size;
}

static uint64_t hashmap_hash_bytes(const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hashmap_hash_key(hashmap_key_t key) {
    uint64_t hash = hashmap_hash_bytes(&key.type, sizeof(key.type));
    uint64_t key_hash = hashmap_hash_bytes(key.data, hashmap_key_size(key));
    return hash ^ (key_hash + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2));
}

static int hashmap_key_equals(hashmap_entry_t *entry, hashmap_key_t key) {
    size_t key_size = hashmap_key_size(key);
    return entry->key_type == key.type &&
           entry->key_size == key_size &&
           hashmap_memeq(entry->key, key.data, key_size);
}

static hashmap_entry_t *hashmap_find_entry(HashMap *map,
                                           hashmap_key_t key,
                                           hashmap_entry_t **prev) {
    if (prev != NULL) {
        *prev = NULL;
    }

    if (map == NULL || map->bucket_count == 0 || key.data == NULL) {
        return NULL;
    }

    size_t bucket = hashmap_hash_key(key) % map->bucket_count;
    hashmap_entry_t *entry = map->buckets[bucket];
    hashmap_entry_t *last = NULL;
    while (entry != NULL) {
        if (hashmap_key_equals(entry, key)) {
            if (prev != NULL) {
                *prev = last;
            }
            return entry;
        }
        last = entry;
        entry = entry->next;
    }

    if (prev != NULL) {
        *prev = last;
    }
    return NULL;
}

HashMap hashmap_new(size_t bucket_count, hashmap_dtor_fn value_dtor) {
    if (bucket_count == 0) {
        bucket_count = 16;
    }

    hashmap_entry_t **buckets = kmalloc(sizeof(hashmap_entry_t *) * bucket_count);
    if (buckets == NULL) {
        fatal_exception("Failed to allocate hashmap buckets.");
    }

    for (size_t i = 0; i < bucket_count; i++) {
        buckets[i] = NULL;
    }

    return (HashMap) {
        .buckets = buckets,
        .bucket_count = bucket_count,
        .size = 0,
        .value_dtor = value_dtor
    };
}

void hashmap_clear(HashMap *map) {
    if (map == NULL || map->buckets == NULL) {
        return;
    }

    for (size_t i = 0; i < map->bucket_count; i++) {
        hashmap_entry_t *entry = map->buckets[i];
        while (entry != NULL) {
            hashmap_entry_t *next = entry->next;
            if (map->value_dtor != NULL) {
                map->value_dtor(entry->value);
            }
            kfree(entry->key);
            kfree(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

void hashmap_destroy(HashMap *map) {
    if (map == NULL) {
        return;
    }

    hashmap_clear(map);
    kfree(map->buckets);
    map->buckets = NULL;
    map->bucket_count = 0;
}

bool hashmap_put(HashMap *map, hashmap_key_t key, hashmap_value_t value) {
    if (map == NULL || map->buckets == NULL || key.data == NULL) {
        return false;
    }

    hashmap_entry_t *entry = hashmap_find_entry(map, key, NULL);
    if (entry != NULL) {
        if (map->value_dtor != NULL) {
            map->value_dtor(entry->value);
        }
        entry->value = value;
        return true;
    }

    size_t key_size = hashmap_key_size(key);
    void *key_copy = kmalloc(key_size);
    if (key_copy == NULL) {
        return false;
    }
    hashmap_memcpy(key_copy, key.data, key_size);

    entry = kmalloc(sizeof(hashmap_entry_t));
    if (entry == NULL) {
        kfree(key_copy);
        return false;
    }

    size_t bucket = hashmap_hash_key(key) % map->bucket_count;
    *entry = (hashmap_entry_t) {
        .key_type = key.type,
        .key_size = key_size,
        .key = key_copy,
        .value = value,
        .next = map->buckets[bucket]
    };
    map->buckets[bucket] = entry;
    map->size++;
    return true;
}

bool hashmap_get(HashMap *map, hashmap_key_t key, hashmap_value_t *value) {
    hashmap_entry_t *entry = hashmap_find_entry(map, key, NULL);
    if (entry == NULL) {
        return false;
    }

    if (value != NULL) {
        *value = entry->value;
    }
    return true;
}

bool hashmap_contains(HashMap *map, hashmap_key_t key) {
    return hashmap_find_entry(map, key, NULL) != NULL;
}

bool hashmap_remove(HashMap *map, hashmap_key_t key, hashmap_value_t *value) {
    hashmap_entry_t *prev;
    hashmap_entry_t *entry = hashmap_find_entry(map, key, &prev);
    if (entry == NULL) {
        return false;
    }

    size_t bucket = hashmap_hash_key(key) % map->bucket_count;
    if (prev == NULL) {
        map->buckets[bucket] = entry->next;
    } else {
        prev->next = entry->next;
    }

    if (value != NULL) {
        *value = entry->value;
    } else if (map->value_dtor != NULL) {
        map->value_dtor(entry->value);
    }

    kfree(entry->key);
    kfree(entry);
    map->size--;
    return true;
}
