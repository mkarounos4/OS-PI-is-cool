#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("touch", "usage: touch <file>...", -EINVAL);
        return -EINVAL;
    }

    int err = touch(argv + 1);
    if (err < 0) {
        print_errno("touch", "failed", err);
    }
    return err;
}
