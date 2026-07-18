#pragma once

#include "data-structs/ring_buffer.h"
#include "devices.h"
#include "data-structs/vec.h"
#include "memory/malloc.h"
#include "scheduler/process.h"
#include "scheduler/scheduler.h"
#include "fs/oft.h"

#define MAX_TTY_DEVICES 8
#define TTY_INPUT_BUFFER_SIZE 4096

#define TTY_MODE_CANONICAL 0
#define TTY_MODE_RAW       1

struct tty_device {
    uint32_t minor;
    char name[32];
    struct dev_st device_number;

    struct RingBuffer rx;
    struct RingBuffer tx;

    Vec rx_wait_queue;
    Vec tx_wait_queue;

    int refcount;

    pid_t fg_pgid;
    uint8_t active;
    uint8_t mode;

    uint8_t session_active;
    uint8_t session_suspended;
    pid_t session_owner_pid;
    pid_t session_owner_pgid;
    pid_t session_saved_fg_pgid;
    uint8_t session_saved_mode;
    uint8_t session_resume_mode;
    int session_saved_active_terminal;
    int session_saved_cursor_row;
    int session_saved_cursor_col;
    int session_suspended_cursor_row;
    int session_suspended_cursor_col;
    char *session_saved_cells;
    char *session_suspended_cells;

    char input_buffer[TTY_INPUT_BUFFER_SIZE];
    size_t input_len;
    size_t input_cursor;
    uint8_t escape_state;
};

struct tty_driver_state {
    struct tty_device *devices[MAX_TTY_DEVICES];
    uint16_t num_ttys;
    // TODO add lock
};

void tty_send_input(int minor, const char *buffer, size_t count);
int tty_drivers_init(void);
int tty_create_device_nodes(void);
int tty_create();
int tty_delete(int minor);
int tty_pop_shell_request(void);
int tcsetpgrp(int fd, pid_t pgid);
int tty_set_mode(int fd, int mode);
int tty_get_mode(int fd);
int tty_get_screen_size(int fd, int *rows, int *cols);
int tty_screen_enter(int fd);
int tty_screen_leave(int fd);
int tty_screen_present(int fd, const char *cells, size_t count,
                       int cursor_row, int cursor_col);
void tty_session_process_exit(pid_t pid);
void tty_session_process_stop(pid_t pid);
void tty_session_process_continue(pid_t pid);
int tty_write(struct oft_entry *entry, const char *buffer, size_t count);
int tty_format_proc(char *buf, size_t size);
