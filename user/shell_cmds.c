#include "shell_cmds.h"

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
