#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    write(STDOUT_FILENO, "\f", 1);
    return 0;
}
