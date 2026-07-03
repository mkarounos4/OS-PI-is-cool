#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    return cat(argv + 1, NULL, 0);
}
