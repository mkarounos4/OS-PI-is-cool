#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    int err = 0;
    for (int i = 1; i < argc; i++) {
        char *paths[] = {argv[i], NULL};
        err = rm(paths);
        if (err < 0) {
            print_errno("rm", argv[i], err);
            return err;
        }
    }
    return err;
}
