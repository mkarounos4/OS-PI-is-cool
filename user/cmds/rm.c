#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    (void)argc;
    int err = rm(argv + 1);
    if (err < 0) {
        print_errno("rm", "failed", err);
    }
    return err;
}
