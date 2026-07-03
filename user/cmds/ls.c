#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    ls(argv[1], STDOUT_FILENO);
    return 0;
}
