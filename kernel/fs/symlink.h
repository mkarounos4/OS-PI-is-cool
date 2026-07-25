#pragma once

#include "devices.h"

extern struct file_operations symlink_fops;

#define SYMLINK_FOPS (&symlink_fops)

int symlink_open(struct oft_entry *entry);
int symlink_close(struct oft_entry *entry);
int symlink_read(struct oft_entry *entry, char *buffer, size_t count);
int symlink_write(struct oft_entry *entry, const char *buffer, size_t count);
int symlink_lookup(const char *f_name, struct fs_dirent *dirent, int curr_dir);
int symlink_readdir(struct oft_entry *dir, struct fs_dirent *out);
int symlink_getattr(int curr_dir, const char *name);
int symlink_readlink(const char *path, char *buffer, size_t count);
