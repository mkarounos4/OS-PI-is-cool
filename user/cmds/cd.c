#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("cd", "usage: cd <dir>", -EINVAL);
        return -EINVAL;
    }

    int err = cd(argv[1]);
    if (err < 0) {
        print_errno("cd", argv[1], err);
    }
    return err;
}
