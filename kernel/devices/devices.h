#pragma once

#include "inodes.h"

struct file_operations {
    int (*open)(struct oft_entry *entry);
    int (*close)(struct oft_entry *entry);
    ssize_t (*read)(struct oft_entry *entry, void *buffer, size_t count);
    ssize_t (*write)(struct oft_entry *entry, const void *buffer, size_t count);
}

typedef struct dev_st {
    uint16_t major;
    uint16_t minor;
} dev_t;

struct char_driver {
    const char *name;
    uint16_t major;
    const struct file_operations *fops;
    void *driver_data;
}

void initialize_char_device_registry();
void destroy_char_device_registry();
void register_char_driver(struct char_driver *driver);
int devfs_create_char_device(dev_t rdev);
struct char_driver *get_char_device(uint16_t major);