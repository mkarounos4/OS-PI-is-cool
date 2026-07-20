#include "devices.h"

#define MAX_CHAR_DEVICES 16

static struct char_driver *char_device_registry[MAX_CHAR_DEVICES];

void initialize_char_device_registry() {
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        char_device_registry[i] = NULL;
    }
}

void destroy_char_device_registry() {
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        if (char_device_registry[i] != NULL) {
            kfree(char_device_registry[i]);
        }
    }
}

int register_char_driver(struct char_driver *driver) {
    if (driver == NULL || driver->fops == NULL) {
        return -1;
    }
    if (driver->major >= MAX_CHAR_DEVICES) {
        return -1;
    }
    if (char_device_registry[driver->major] != NULL) {
        return -2;
    }

    char_device_registry[driver->major] = driver;
    return 0;
}

struct char_driver *get_char_device(uint16_t major) {
    if (major >= MAX_CHAR_DEVICES) {
        return NULL;
    }
    return char_device_registry[major];
}

static void make_device_entry(struct dev_st rdev, struct oft_entry *entry,
                              struct cached_inode_st *cache) {
    kmemset(entry, 0, sizeof(*entry));
    kmemset(cache, 0, sizeof(*cache));
    cache->inode.metadata.type = CHAR_DRIVER_TYPE;
    cache->inode.metadata.i_rdev = rdev;
    entry->inode = cache;
}

int char_device_read(struct dev_st rdev, char *buffer, size_t count) {
    struct char_driver *driver = get_char_device(rdev.major);
    if (driver == NULL || driver->fops == NULL || driver->fops->read == NULL) {
        return -1;
    }

    struct oft_entry entry;
    struct cached_inode_st cache;
    make_device_entry(rdev, &entry, &cache);
    return driver->fops->read(&entry, buffer, count);
}

int char_device_write(struct dev_st rdev, const char *buffer, size_t count) {
    struct char_driver *driver = get_char_device(rdev.major);
    if (driver == NULL || driver->fops == NULL || driver->fops->write == NULL) {
        return -1;
    }

    struct oft_entry entry;
    struct cached_inode_st cache;
    make_device_entry(rdev, &entry, &cache);
    return driver->fops->write(&entry, buffer, count);
}
