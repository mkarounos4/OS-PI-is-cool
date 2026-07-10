#include "lib/errno.h"
#include "lib/signals.h"
#include "lib/string.h"
#include "lib/syscall.h"

static int parse_int(const char *s, int *out) {
    char *end;
    long value = strtol(s, &end, 10);
    if (s == end || *end != '\0') {
        return -EINVAL;
    }
    *out = (int)value;
    return 0;
}

int main(int argc, char **argv) {
    int signal = SIGTERM;
    int pid_arg = 1;

    if (argc < 2) {
        print_errno("kill", "usage: kill [-signal] <pid>", -EINVAL);
        return -EINVAL;
    }

    if (argv[1][0] == '-' && argv[1][1] != '\0') {
        int err = parse_int(argv[1] + 1, &signal);
        if (err < 0) {
            print_errno("kill", argv[1], err);
            return err;
        }
        pid_arg = 2;
    }

    if (pid_arg >= argc) {
        print_errno("kill", "missing pid", -EINVAL);
        return -EINVAL;
    }

    int pid;
    int err = parse_int(argv[pid_arg], &pid);
    if (err < 0) {
        print_errno("kill", argv[pid_arg], err);
        return err;
    }

    err = (int)kill((pid_t)pid, signal);
    if (err < 0) {
        print_errno("kill", argv[pid_arg], err);
    }
    return err;
}
