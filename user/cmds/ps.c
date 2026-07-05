#include "lib/syscall.h"
#include "lib/errno.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int err = ps();
    if (err < 0) {
        print_errno("ps", "failed", err);
    }
    return err;
}
