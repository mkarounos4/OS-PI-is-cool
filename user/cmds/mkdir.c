#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("mkdir", "usage: mkdir <dir>...", -EINVAL);
        return -EINVAL;
    }

    int err = fs_mkdir(argv + 1);
    if (err < 0) {
        print_errno("mkdir", "failed", err);
    }
    return err;
}
