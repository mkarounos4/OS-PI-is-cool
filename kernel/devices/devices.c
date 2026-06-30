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
    if (driver->major > MAX_CHAR_DEVICES) {
        return -1;
    }
    if (char_device_registry[driver->major] != NULL) {
        return -2;
    }

    char_device_registry[driver->major] = driver;
    return 0;
}

int devfs_create_char_device(struct dev_st rdev) {
    if (char_device_registry[rdev.major] == NULL) {
        return -1;
    }

    int ino = add_new_file(NULL, CHAR_DRIVER_TYPE, 0x7, char_device_registry[rdev.major]->fops);
    if (ino < 0) {
        return -1;
    }

    struct inode_st inode;
    err_t err = get_inode_raw(&inode, ino);
    if (err != SUCCESS) {
        return err;
    }

    inode.metadata.i_rdev = rdev;
    
    err = write_inode(&inode, ino);
    if (err) {
        return err;
    }

    struct fs_dirent dirent;
    err = get_dirent_by_path("/dev", &dirent, 1, NULL, NULL);
    if (err == FILE_NOT_CREATED) {
        char **paths = kmalloc(sizeof(char*) * 2);
        paths[1] = NULL;
        paths[0] = "/dev";
        err = fs_mkdir((char**)paths);
        kfree(paths);
        if (err) {
            return err;
        }

        err = get_dirent_by_path("/dev", &dirent, 1, NULL, NULL);
        if (err) {
            return err;
        }
    } else if (err) {
        return err;
    }

    char *name = kmalloc(sizeof(char) * 32);
    if (name == NULL) {
        return -1;
    }
    strcpy(name, char_device_registry[rdev.major]->name);
    int len = strlen(char_device_registry[rdev.major]->name);
    if (len > 30) {
        name[30] = '0' + rdev.minor;
        name[31] = '\0';
    } else {
        name[len] = '0'+ rdev.minor;
        name[len+1] = '\0';
    }

    err = add_dirent(name, ino, dirent.ino_id);
    if (err) {
        return err;
    }

    return SUCCESS;
}

struct char_driver *get_char_device(uint16_t major) {
    return char_device_registry[major];
}
