#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        return -1;
    }

    return cp(argv[1], argv[2], 0);
}
