#pragma once

#include <stdint.h>
#include "memory/kmalloc.h"
#include "fs/cmds.h"
#include "fs/oft.h"

typedef __SIZE_TYPE__ size_t;
typedef __SIZE_TYPE__ ssize_t;

struct file_operations {
    int (*open)(struct oft_entry *entry);
    int (*close)(struct oft_entry *entry);
    int (*read)(struct oft_entry *entry, char *buffer, size_t count);
    int (*write)(struct oft_entry *entry, const char *buffer, size_t count);
    int (*lookup)(const char* f_name, uint8_t is_dir_type, struct fs_dirent* dirent, int curr_dir);
    int (*readdir)(struct oft_entry *dir, struct fs_dirent *out);
    int (*getattr)(int curr_dir, const char *name);
};

#ifndef DEV_T_TYPE
#define DEV_T_TYPE
struct dev_st {
    uint16_t major;
    uint16_t minor;
};
#endif

struct char_driver {
    const char *name;
    uint16_t major;
    const struct file_operations *fops;
    void *driver_data;
};

void initialize_char_device_registry();
void destroy_char_device_registry();
int register_char_driver(struct char_driver *driver);
int devfs_create_char_device(struct dev_st rdev);
struct char_driver *get_char_device(uint16_t major);
