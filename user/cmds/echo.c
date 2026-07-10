#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/string.h"

int echo(char **args) {
    int i = 1;
    while (args[i] != NULL) {
        int err = write(1, args[i], strlen(args[i]));
        if (err < 0) {
            return err;
        }
        if (args[i + 1] != NULL) {
            err = write(1, " ", 1);
            if (err < 0) {
                return err;
            }
        }
        i++;
    }

    return write(1, "\n", 1);
}

int main(int argc, char **argv) {
    (void)argc;
    int err = echo(argv);
    if (err < 0) {
        print_errno("echo", "write", err);
    }
    return err < 0 ? err : 0;
}
