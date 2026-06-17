#include "shell.h"

void *shell_init(void *args) {
    int shell_num = (int)(uintptr_t)args;
    char path[10];
    path[0] = '/';
    path[1] = 'd';
    path[2] = 'e';
    path[3] = 'v';
    path[4] = '/';
    path[5] = 't';
    path[6] = 't';
    path[7] = 'y';
    path[8] = '0' + shell_num;
    path[9] = '\0';

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return;
    }

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return;
    }

    // call actual shell handler here.

    return 0;
}
