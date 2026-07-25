#include "lib/errno.h"
#include "lib/fs_syscall.h"

static int usage(void) {
    print_errno("ln", "usage: ln [-s] <target> <link>", -EINVAL);
    return -EINVAL;
}

int main(int argc, char **argv) {
    int flags = LINK_HARD;
    int target_arg = 1;

    if (argc == 4 && argv[1][0] == '-' && argv[1][1] == 's' &&
        argv[1][2] == '\0') {
        flags = LINK_SOFT;
        target_arg = 2;
    } else if (argc != 3) {
        return usage();
    }

    const char *target = argv[target_arg];
    const char *link = argv[target_arg + 1];
    int err = ln(target, link, flags);
    if (err < 0) {
        print_errno("ln", link, err);
    }
    return err;
}
