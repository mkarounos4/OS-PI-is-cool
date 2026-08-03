#include "devices.h"
#include "sync/spinlock.h"

#define MAX_CHAR_DEVICES 16

static struct char_driver *char_device_registry[MAX_CHAR_DEVICES];
static spinlock_t char_device_registry_lock = SPINLOCK_INIT;

void initialize_char_device_registry() {
    uint64_t flags = spin_lock_irqsave(&char_device_registry_lock);
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        char_device_registry[i] = NULL;
    }
    spin_unlock_irqrestore(&char_device_registry_lock, flags);
}

void destroy_char_device_registry() {
    uint64_t flags = spin_lock_irqsave(&char_device_registry_lock);
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        if (char_device_registry[i] != NULL) {
            kfree(char_device_registry[i]);
        }
    }
    spin_unlock_irqrestore(&char_device_registry_lock, flags);
}

int register_char_driver(struct char_driver *driver) {
    if (driver == NULL || driver->fops == NULL) {
        return -1;
    }
    if (driver->major >= MAX_CHAR_DEVICES) {
        return -1;
    }
    uint64_t flags = spin_lock_irqsave(&char_device_registry_lock);
    if (char_device_registry[driver->major] != NULL) {
        spin_unlock_irqrestore(&char_device_registry_lock, flags);
        return -2;
    }

    char_device_registry[driver->major] = driver;
    spin_unlock_irqrestore(&char_device_registry_lock, flags);
    return 0;
}

struct char_driver *get_char_device(uint16_t major) {
    uint64_t flags = spin_lock_irqsave(&char_device_registry_lock);
    if (major >= MAX_CHAR_DEVICES) {
        spin_unlock_irqrestore(&char_device_registry_lock, flags);
        return NULL;
    }
    struct char_driver *driver = char_device_registry[major];
    spin_unlock_irqrestore(&char_device_registry_lock, flags);
    return driver;
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
