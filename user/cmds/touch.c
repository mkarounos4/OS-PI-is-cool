#include "lib/fs_syscall.h"
#include "lib/stdio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("touch: usage: touch <file>...\n");
        return -1;
    }

    return touch(argv + 1);
}
