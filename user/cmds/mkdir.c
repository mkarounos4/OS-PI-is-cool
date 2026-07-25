#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("mkdir", "usage: mkdir <dir>...", -EINVAL);
        return -EINVAL;
    }

    int err = 0;
    for (int i = 1; i < argc; i++) {
        char *paths[] = {argv[i], NULL};
        err = fs_mkdir(paths);
        if (err < 0) {
            print_errno("mkdir", argv[i], err);
            return err;
        }
    }
    return err;
}
