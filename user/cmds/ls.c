#include "lib/fs_syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    char *path = argc > 1 ? argv[1] : NULL;
    int err = ls(path);
    if (err < 0) {
        print_errno("ls", path == NULL ? "." : path, err);
    }
    return err;
}
