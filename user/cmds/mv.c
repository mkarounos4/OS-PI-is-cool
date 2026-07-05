#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        return -EINVAL;
    }

    return mv(argv[1], argv[2]);
}
