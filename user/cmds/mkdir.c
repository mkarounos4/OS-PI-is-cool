#include "lib/fs_syscall.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("mkdir: usage: mkdir <dir>...\n");
        return -1;
    }

    return fs_mkdir(argv + 1);
}
