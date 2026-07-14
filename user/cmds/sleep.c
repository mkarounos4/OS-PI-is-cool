
#include "lib/errno.h"
#include "lib/string.h"
#include "lib/syscall.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        print_errno("sleep", "usage: sleep <milliseconds>", -EINVAL);
        return -EINVAL;
    }

    char *end;
    long milliseconds = strtol(argv[1], &end, 10);
    if (argv[1] == end || *end != '\0' || milliseconds < 0) {
        print_errno("sleep", argv[1], -EINVAL);
        return -EINVAL;
    }

    long err = sleep((uint64_t)milliseconds);
    if (err < 0) {
        print_errno("sleep", argv[1], err);
    }

    return (int)err;
}
