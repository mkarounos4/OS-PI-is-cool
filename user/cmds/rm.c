#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    return rm(argv + 1);
}
