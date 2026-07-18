#pragma once

#include <stddef.h>
#include <stdint.h>

#include "syscall.h"

#define TTY_MODE_CANONICAL 0
#define TTY_MODE_RAW       1

static inline int tty_get_mode(int fd) {
    return (int)sys_call1(S_TTY_GET_MODE, fd);
}

static inline int tty_set_mode(int fd, int mode) {
    return (int)sys_call2(S_TTY_SET_MODE, fd, mode);
}

static inline int tty_get_size(int fd, int *rows, int *cols) {
    return (int)sys_call3(S_TTY_GET_SIZE, fd,
                          (long)(uintptr_t)rows,
                          (long)(uintptr_t)cols);
}

static inline int tty_screen_enter(int fd) {
    return (int)sys_call1(S_TTY_SCREEN_ENTER, fd);
}

static inline int tty_screen_leave(int fd) {
    return (int)sys_call1(S_TTY_SCREEN_LEAVE, fd);
}

static inline int tty_screen_present(int fd, const char *cells, size_t count,
                                     int cursor_row, int cursor_col) {
    return (int)sys_call5(S_TTY_SCREEN_PRESENT, fd,
                          (long)(uintptr_t)cells, (long)count,
                          cursor_row, cursor_col);
}
