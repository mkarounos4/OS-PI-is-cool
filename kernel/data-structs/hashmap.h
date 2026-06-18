#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *hashmap_value_t;
typedef void (*hashmap_dtor_fn)(hashmap_value_t value);

typedef enum hashmap_key_type {
    HASHMAP_KEY_INT,
    HASHMAP_KEY_UINT64,
    HASHMAP_KEY_CHAR,
    HASHMAP_KEY_STRING,
    HASHMAP_KEY_BYTES,
} hashmap_key_type_t;

typedef struct hashmap_key {
    hashmap_key_type_t type;
    const void *data;
    size_t size;
} hashmap_key_t;

typedef struct hashmap_entry {
    hashmap_key_type_t key_type;
    size_t key_size;
    void *key;
    hashmap_value_t value;
    struct hashmap_entry *next;
} hashmap_entry_t;

typedef struct hashmap {
    hashmap_entry_t **buckets;
    size_t bucket_count;
    size_t size;
    hashmap_dtor_fn value_dtor;
} HashMap;

HashMap hashmap_new(size_t bucket_count, hashmap_dtor_fn value_dtor);
void hashmap_destroy(HashMap *map);
void hashmap_clear(HashMap *map);

bool hashmap_put(HashMap *map, hashmap_key_t key, hashmap_value_t value);
bool hashmap_get(HashMap *map, hashmap_key_t key, hashmap_value_t *value);
bool hashmap_contains(HashMap *map, hashmap_key_t key);
bool hashmap_remove(HashMap *map, hashmap_key_t key, hashmap_value_t *value);

#define HASHMAP_KEY_FROM_INT(value) \
    ((hashmap_key_t){HASHMAP_KEY_INT, &(value), sizeof(value)})

#define HASHMAP_KEY_FROM_UINT64(value) \
    ((hashmap_key_t){HASHMAP_KEY_UINT64, &(value), sizeof(value)})

#define HASHMAP_KEY_FROM_CHAR(value) \
    ((hashmap_key_t){HASHMAP_KEY_CHAR, &(value), sizeof(value)})

#define HASHMAP_KEY_FROM_STRING(value) \
    ((hashmap_key_t){HASHMAP_KEY_STRING, (value), 0})

#define HASHMAP_KEY_FROM_BYTES(value, len) \
    ((hashmap_key_t){HASHMAP_KEY_BYTES, (value), (len)})
