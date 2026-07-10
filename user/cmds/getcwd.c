#include <stdint.h>

#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/stdio.h"
#include "lib/string.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char path[256];
    char *ret = getcwd(path, sizeof(path));
    if ((intptr_t)ret < 0) {
        print_errno("getcwd", "failed", (long)(intptr_t)ret);
        return (int)(intptr_t)ret;
    }

    write(STDOUT_FILENO, path, (int)strlen(path));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}
