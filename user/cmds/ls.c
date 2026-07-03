#include "lib/fs_syscall.h"

int main(int argc, char **argv) {
    return ls(argc > 1 ? argv[1] : NULL);
}
