#include "lib/errno.h"
#include "lib/fs_syscall.h"

static int usage(void) {
    print_errno("readlink", "usage: readlink <path>", -EINVAL);
    return -EINVAL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return usage();
    }

    char buffer[1024];
    int bytes = readlink(argv[1], buffer, sizeof(buffer) - 1);
    if (bytes < 0) {
        print_errno("readlink", argv[1], bytes);
        return bytes;
    }

    buffer[bytes] = '\0';
    int written = write(STDOUT_FILENO, buffer, bytes);
    if (written < 0) {
        print_errno("readlink", argv[1], written);
        return written;
    }
    written = write(STDOUT_FILENO, "\n", 1);
    if (written < 0) {
        print_errno("readlink", argv[1], written);
        return written;
    }
    return 0;
}
