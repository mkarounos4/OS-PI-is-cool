#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        return -EINVAL;
    }

    return cd(argv[1]);
}
