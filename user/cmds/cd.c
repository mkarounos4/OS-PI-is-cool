#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    if (argc < 1) {
        return -1;
    }

    cd(argv[1]);
    return 0;
}
