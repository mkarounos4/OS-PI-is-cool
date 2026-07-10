
#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/stdio.h"

#define STAT_DIRECTORY_TYPE 0
#define STAT_FILE_TYPE 1
#define STAT_SYMLINK_TYPE 2
#define STAT_CHAR_DRIVER_TYPE 3
#define STAT_PIPE_TYPE 4

static const char *type_name(uint8_t type) {
    switch (type) {
    case STAT_DIRECTORY_TYPE:
        return "directory";
    case STAT_FILE_TYPE:
        return "file";
    case STAT_SYMLINK_TYPE:
        return "symlink";
    case STAT_CHAR_DRIVER_TYPE:
        return "char";
    case STAT_PIPE_TYPE:
        return "pipe";
    default:
        return "unknown";
    }
}

static int stat_one(const char *path) {
    struct fs_stat_st st;
    int err = stat(path, &st);
    if (err < 0) {
        print_errno("stat", path, err);
        return err;
    }

    printf("File: %s\n", path);
    printf("Inode: %u\n", st.ino_id);
    printf("Type: %s (%u)\n", type_name(st.type), st.type);
    printf("Permissions: %u\n", st.perm);
    printf("Links: %u\n", st.links_count);
    printf("Size: %u\n", st.size);
    printf("Blocks: %u\n", st.blocks);
    printf("Mtime: %u\n", (unsigned int)st.mtime);
    if (st.type == STAT_CHAR_DRIVER_TYPE) {
        printf("Device: %u:%u\n", st.rdev_major, st.rdev_minor);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("stat", "usage: stat <file>...", -EINVAL);
        return -EINVAL;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int err = stat_one(argv[i]);
        if (err < 0) {
            ret = err;
        }
    }
    return ret;
}
