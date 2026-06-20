#include "tty.h"

#define TTY_MAJOR 0

int tty_open(struct oft_entry *entry);
int tty_close(struct oft_entry *entry);
ssize_t tty_read(struct oft_entry *entry, void *buffer, size_t count);
ssize_t tty_write(struct oft_entry *entry, const void *buffer, size_t count);

static struct tty_driver_state tty_state;

static const struct file_operations tty_fops = {
    .open = tty_open,
    .close = tty_close,
    .read = tty_read,
    .write = tty_write,
};

static struct char_driver tty_char_driver = {
    .name = "tty",
    .major = TTY_MAJOR,
    .fops = &tty_fops,
    .driver_data = (void*) &tty_state,
};

int tty_drivers_init(void) {
    kmemset(&tty_state, 0, sizeof(struct tty_driver_state));

    return register_char_driver(&tty_char_driver);
}

int tty_create() {
    if (tty_state.num_ttys == MAX_TTY_DEVICES) {
        return -1;
    }

    uint16_t minor = tty_state.num_ttys++;
    struct tty_device *new_tty = kmalloc(sizeof(struct tty_device));
    if (new_tty == NULL) {
        return -2;
    }
    tty_state.devices[minor] = new_tty;

    new_tty->name[0] = 't';
    new_tty->name[1] = 't';
    new_tty->name[2] = 'y';
    new_tty->name[3] = '0' + minor;
    new_tty->name[4] = '\0';

    new_tty->device_number = (struct dev_st) {
        .major = TTY_MAJOR,
        .minor = minor,
    };
    
    new_tty->rx = create_ring_buffer(4096);
    new_tty->tx = create_ring_buffer(4096);
    
    new_tty->rx_wait_queue = vec_new(2, NULL);
    new_tty->tx_wait_queue = vec_new(2, NULL);

    new_tty->fg_pgid = 0;

    err_t err = devfs_create_char_device(new_tty->device_number);
    if (err) {
        return err;
    }

    return minor;
}

int tty_open(struct oft_entry *entry) {
    return 0;
}

int tty_close(struct oft_entry *entry) {
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    tty_state.devices[minor]->refcount--;
    if (tty_state.devices[minor]->refcount == 0) {
        vec_destroy(&tty_state.devices[minor]->rx_wait_queue);
        vec_destroy(&tty_state.devices[minor]->tx_wait_queue);
        destroy_ring_buffer(&tty_state.devices[minor]->rx);
        destroy_ring_buffer(&tty_state.devices[minor]->tx);
        kfree(tty_state.devices[minor]);
        // TODO: somehow free /dev/ttyx file
    }

    return 0;
}

ssize_t tty_read(struct oft_entry *entry, char *buffer, size_t count) {
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;

    struct tty_device *curr_tty = tty_state.devices[minor];

    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return 0;
    }
    if (curr_pcb->pgid != curr_tty->fg_pgid) {
        s_kill(-curr_pcb->pgid, SIGTTIN);
    }
    
    ssize_t num_read = 0;
    while (num_read < count) {
        char *char_void;
        bool read_char = consume_ring_buffer(&curr_tty->rx, char_void);
        if (!read_char) {
            vec_push_back(&curr_tty->rx_wait_queue, (ptr_t)(uintptr_t)curr_pcb->pid);
            block_process(curr_pcb);
            continue;
        }

        if (read_char == '\n') {
            return num_read;
        }
        *buffer = *char_void;
        buffer++;
        num_read++;
    }

    return num_read;
}

ssize_t tty_write(struct oft_entry *entry, const char *buffer, size_t count) {
    /*uint16_t minor = entry->inode->inode.metadata.i_rdev.major;

    struct tty_device *curr_tty = tty_state.devices[minor];

    ssize_t num_written = 0;
    while (num_written < count) {
        bool wrote_char = produce_ring_buffer(curr_tty->tx, buffer);
        if (!wrote_char) {
            vec_push_back(&curr_tty->tx_wait_queue, get_curr_pid());
            block_self();
            continue;
        }
        buffer++;
        num_written++;
    }
    */

    ssize_t num_written = 0;
    while(num_written < count) {
        uart_putc(*buffer);
        buffer++;
        num_written++;
    }

    return num_written;
}

void tty_send_input(int minor, const char *buffer, size_t count) {
    if (buffer == NULL || tty_state.num_ttys <= minor) {
        return;
    }

    while (count > 0) {
        if (*buffer == 0x03) {
            s_kill(SIGINT, -tty_state.devices[minor]->fg_pgid);
            bool wrote_char = produce_ring_buffer(&tty_state.devices[minor]->tx, "^");
            if (!wrote_char) {
                return;
            }
            bool wrote_char = produce_ring_buffer(&tty_state.devices[minor]->tx, "C");
            if (!wrote_char) {
                return;
            }
        } else if (*buffer == 0x1A) {
            s_kill(SIGTSTP, -tty_state.devices[minor]->fg_pgid);
            bool wrote_char = produce_ring_buffer(&tty_state.devices[minor]->tx, "^");
            if (!wrote_char) {
                return;
            }
            bool wrote_char = produce_ring_buffer(&tty_state.devices[minor]->tx, "Z");
            if (!wrote_char) {
                return;
            }
        } else {
            bool wrote_char = produce_ring_buffer(&tty_state.devices[minor]->tx, buffer);
            if (!wrote_char) {
                return;
            }
        }

        buffer++;
        count--;
    }
}

int tcsetpgrp(int fd, int pgid) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (pgid == 0) {
        pgid = pcb->pgid;
    }

    if (tty_state.devices[fd]->fg_pgid != pcb->pgid) {
        s_kill(pcb->pid, SIGTTOU);
    }
    tty_state.devices[fd]->fg_pgid = pgid;
    return 0;
}
