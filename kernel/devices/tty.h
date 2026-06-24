#pragma once

#include "data-structs/ring_buffer.h"
#include "devices.h"
#include "data-structs/vec.h"
#include "memory/malloc.h"
#include "scheduler/process.h"
#include "scheduler/scheduler.h"
#include "fs/oft.h"

#define MAX_TTY_DEVICES 16

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
};

struct tty_driver_state {
    struct tty_device *devices[MAX_TTY_DEVICES];
    uint16_t num_ttys;
    // TODO add lock
};

void tty_send_input(int minor, const char *buffer, size_t count);
int tty_drivers_init(void);
int tty_create();
int tcsetpgrp(int fd, pid_t pgid);
ssize_t tty_write(struct oft_entry *entry, const char *buffer, size_t count);

