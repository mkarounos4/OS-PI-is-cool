#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        print_errno("chmod", "usage: chmod <mode> <file>", -EINVAL);
        return -EINVAL;
    }

    int flag = 0;
    char *new_perms = argv[1];
    if (argv[1][0] == '-') {
        flag = 1;
        new_perms = argv[1] + 1;
    } else if (argv[1][0] == '+') {
        flag = 2;
        new_perms = argv[1] + 1;
    } else if (argv[1][0] == '=') {
        flag = 0;
        new_perms = argv[1] + 1;
    }

    int err = fs_chmod(argv[2], new_perms, flag);
    if (err < 0) {
        print_errno("chmod", argv[2], err);
    }
    return err;
}
