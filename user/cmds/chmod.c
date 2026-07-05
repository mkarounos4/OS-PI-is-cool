#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("chmod: usage: chmod <mode> <file>\n");
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

    return fs_chmod(argv[2], new_perms, flag);
}
