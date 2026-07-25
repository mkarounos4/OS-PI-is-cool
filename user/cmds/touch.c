#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("touch", "usage: touch <file>...", -EINVAL);
        return -EINVAL;
    }

    int err = 0;
    for (int i = 1; i < argc; i++) {
        char *paths[] = {argv[i], NULL};
        err = touch(paths);
        if (err < 0) {
            print_errno("touch", argv[i], err);
            return err;
        }
    }
    return err;
}
