#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("mkdir: usage: mkdir <dir>...\n");
        return -EINVAL;
    }

    return fs_mkdir(argv + 1);
}
