#include "lib/fs_syscall.h"
#include "lib/string.h"

void echo(char **args) {
    int i = 1;
    while (args[i] != NULL) {
        write(1, args[i], strlen(args[i]));
        if (args[i + 1] != NULL) {
            write(1, " ", 1);
        }
        i++;
    }

    write(1, "\n", 1);
}

int main(int argc, char **argv) {
    (void)argc;
    echo(argv);
    return 0;
}
