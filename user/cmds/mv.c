#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        print_errno("mv", "usage: mv <src> <dest>", -EINVAL);
        return -EINVAL;
    }

    int err = mv(argv[1], argv[2]);
    if (err < 0) {
        print_errno("mv", "failed", err);
    }
    return err;
}
