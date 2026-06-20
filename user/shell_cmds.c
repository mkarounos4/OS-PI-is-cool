#include "shell_cmds.h"

void echo(char **args) {
    char result[1000] = "";
    
    int i = 1;
    while (args[i] != NULL) {
        str_concat(result, args[i]);
        str_concat(result, " ");
        i++;
    }

    write(1, result, strlen(result));
    write(1, "\n", 1);

    return;
}
