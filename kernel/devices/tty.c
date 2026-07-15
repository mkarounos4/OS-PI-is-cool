#include "tty.h"
#include "gui/tty_gui.h"
#include "scheduler.h"
#include "threading/thread.h"
#include "uart/uart.h"

#define TTY_MAJOR 0

int tty_open(struct oft_entry *entry);
int tty_close(struct oft_entry *entry);
int tty_read(struct oft_entry *entry, char *buffer, size_t count);
static void tty_clear_input_line(struct tty_device *tty);
void wake_up_readers(int minor);

static struct tty_driver_state tty_state;
static int pending_shell_ttys[MAX_TTY_DEVICES];
static int pending_shell_count;
static int pending_terminal_creates;
static uint8_t tty_nodes_ready;

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
    pending_shell_count = 0;
    pending_terminal_creates = 0;
    tty_nodes_ready = 0;

    return register_char_driver(&tty_char_driver);
}

int tty_create_device_nodes(void) {
    for (int minor = 0; minor < MAX_TTY_DEVICES; minor++) {
        struct dev_st device_number = {
            .major = TTY_MAJOR,
            .minor = (uint16_t)minor,
        };
        err_t err = devfs_create_char_device(device_number);
        if (err) {
            return err;
        }
    }

    tty_nodes_ready = 1;
    return 0;
}

int tty_pop_shell_request(void) {
    if (pending_terminal_creates > 0) {
        pending_terminal_creates--;
        int created = tty_create();
        if (created >= 0) {
            tty_gui_activate_terminal(created);
            return created;
        }
    }

    if (pending_shell_count == 0) {
        return -1;
    }

    int minor = pending_shell_ttys[0];
    for (int i = 1; i < pending_shell_count; i++) {
        pending_shell_ttys[i - 1] = pending_shell_ttys[i];
    }
    pending_shell_count--;
    return minor;
}

int tty_create() {
    if (tty_state.num_ttys == MAX_TTY_DEVICES) {
        return -1;
    }
    if (!tty_nodes_ready) {
        return -1;
    }

    int minor = -1;
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        if (tty_state.devices[i] == NULL || !tty_state.devices[i]->active) {
            minor = i;
            break;
        }
    }
    if (minor < 0) {
        return -1;
    }

    struct tty_device *new_tty = tty_state.devices[minor];
    if (new_tty == NULL) {
        new_tty = kmalloc(sizeof(struct tty_device));
        if (new_tty == NULL) {
            return -2;
        }
        tty_state.devices[minor] = new_tty;
        kmemset(new_tty, 0, sizeof(struct tty_device));
        new_tty->rx = create_ring_buffer(4096);
        new_tty->tx = create_ring_buffer(4096);
        new_tty->rx_wait_queue = vec_new(2, NULL);
        new_tty->tx_wait_queue = vec_new(2, NULL);
    }

    new_tty->name[0] = 't';
    new_tty->name[1] = 't';
    new_tty->name[2] = 'y';
    new_tty->name[3] = '0' + minor;
    new_tty->name[4] = '\0';

    new_tty->device_number = (struct dev_st) {
        .major = TTY_MAJOR,
        .minor = minor,
    };

    new_tty->fg_pgid = 0;
    new_tty->active = 0;
    new_tty->input_len = 0;
    new_tty->input_cursor = 0;
    new_tty->escape_state = 0;
    new_tty->rx.head = NULL;
    new_tty->rx.tail = NULL;
    new_tty->rx.size = 0;
    new_tty->tx.head = NULL;
    new_tty->tx.tail = NULL;
    new_tty->tx.size = 0;

    new_tty->active = 1;
    tty_state.num_ttys++;
    tty_gui_set_terminal_visible(minor, 1);
    return minor;
}

int tty_open(struct oft_entry *entry) {
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= MAX_TTY_DEVICES || tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active) {
        return -1;
    }
    tty_state.devices[minor]->refcount++;
    return 0;
}

int tty_close(struct oft_entry *entry) {
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= MAX_TTY_DEVICES || tty_state.devices[minor] == NULL) {
        return -1;
    }
    tty_state.devices[minor]->refcount--;
    if (tty_state.devices[minor]->refcount < 0) {
        tty_state.devices[minor]->refcount = 0;
    }

    return 0;
}

int tty_read(struct oft_entry *entry, char *buffer, size_t count) {
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;

    struct tty_device *curr_tty = tty_state.devices[minor];
    if (minor >= MAX_TTY_DEVICES || curr_tty == NULL || !curr_tty->active) {
        return 0;
    }

    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return 0;
    }
    if (curr_pcb->pgid != curr_tty->fg_pgid) {
        s_kill(-curr_pcb->pgid, SIGTTIN);
    }
    

    tcb_t *curr_thd = get_curr_thread();

    ssize_t num_read = 0;
    while (num_read < count) {
        char char_void;
        bool read_char = consume_ring_buffer(&curr_tty->rx, &char_void);
        if (!read_char) {
            vec_push_back(&curr_tty->rx_wait_queue, (ptr_t)(uintptr_t)curr_thd->tid);
            block_thread(curr_thd, THREAD_BLOCKED_INTERRUPTABLE);
            continue;
        }

        if (char_void == 0x04) {
            return num_read;
        }

        *buffer = char_void;
        buffer++;
        num_read++;

        if (char_void == '\n' || char_void == 0x0C) {
            return num_read;
        }

    }

    return num_read;
}

int tty_write(struct oft_entry *entry, const char *buffer, size_t count) {
    uint16_t minor = (uint16_t)tty_gui_get_active_terminal();
    if (entry != NULL) {
        minor = entry->inode->inode.metadata.i_rdev.minor;
    }
    if (minor >= MAX_TTY_DEVICES || tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active) {
        return 0;
    }
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
#ifdef UART_OUT
        printf("%c", *buffer);
#else
        tty_gui_write_char_for_tty(minor, *buffer);
#endif
        buffer++;
        num_written++;
    }

    return num_written;
}

int tty_format_proc(char *buf, size_t size) {
    if (buf == NULL || size == 0) {
        return INVALID_ARGS;
    }

    int len = snprintf(buf, size, "ttys: %u\n", tty_state.num_ttys);
    for (uint16_t minor = 0; minor < MAX_TTY_DEVICES; minor++) {
        struct tty_device *tty = tty_state.devices[minor];
        if (tty == NULL || !tty->active) {
            continue;
        }

        size_t used = len < (int)size ? (size_t)len : size - 1;
        int ret = snprintf(buf + used, size - used,
                           "name: %s\n"
                           "foreground_pgid: %d\n"
                           "input_backend: uart\n"
                           "output_backend: framebuffer\n"
                           "rows: %d\n"
                           "cols: %d\n"
                           "cursor: %d,%d\n"
                           "input_buffer: %d\n"
                           "output_buffer: %d\n"
                           "refcount: %d\n"
                           "canonical_mode: yes\n",
                           tty->name,
                           tty->fg_pgid,
                           tty_gui_get_rows(),
                           tty_gui_get_cols(),
                           tty_gui_get_cursor_row(),
                           tty_gui_get_cursor_col(),
                           tty->rx.size,
                           tty->tx.size,
                           tty->refcount);
        if (ret < 0) {
            return ret;
        }
        len += ret;
    }

    return len;
}

void wake_up_readers(int minor) {
    while (!vec_is_empty(&tty_state.devices[minor]->rx_wait_queue)) {
        void *tid;
        int removed = (int)(uintptr_t)vec_pop_back(&tty_state.devices[minor]->rx_wait_queue, &tid);
        if (!removed) {
            continue;
        }
        unblock_thread(thread_get_by_tid((int)(uintptr_t)tid));
    }
}

static int tty_visible_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        if (tty_state.devices[i] != NULL &&
            tty_state.devices[i]->active &&
            tty_gui_is_terminal_visible(i)) {
            count++;
        }
    }
    return count;
}

static int tty_show_hidden_terminal(void) {
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        if (tty_state.devices[i] != NULL &&
            tty_state.devices[i]->active &&
            !tty_gui_is_terminal_visible(i)) {
            tty_gui_set_terminal_visible(i, 1);
            tty_gui_activate_terminal(i);
            return i;
        }
    }
    return -1;
}

int tty_delete(int minor) {
    if (minor < 0 || minor >= MAX_TTY_DEVICES ||
        tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active ||
        !tty_gui_is_terminal_visible(minor) ||
        tty_visible_count() <= 1) {
        return -1;
    }

    tty_gui_set_terminal_visible(minor, 0);
    return 0;
}

static void tty_move_cursor_left(size_t count) {
    while (count > 0) {
        tty_gui_cursor_left();
        count--;
    }
}

static void tty_move_cursor_right(size_t count) {
    while (count > 0) {
        tty_gui_cursor_right();
        count--;
    }
}

static void tty_redraw_from_cursor(struct tty_device *tty, size_t cursor) {
    for (size_t i = cursor; i < tty->input_len; i++) {
        tty_write(NULL, &tty->input_buffer[i], 1);
    }
}

static void tty_insert_input_char(struct tty_device *tty, char ch) {
    if (tty->input_len + 1 >= TTY_INPUT_BUFFER_SIZE) {
        return;
    }

    for (size_t i = tty->input_len; i > tty->input_cursor; i--) {
        tty->input_buffer[i] = tty->input_buffer[i - 1];
    }

    tty->input_buffer[tty->input_cursor] = ch;
    tty->input_len++;

    size_t inserted_at = tty->input_cursor;
    tty_redraw_from_cursor(tty, inserted_at);
    tty->input_cursor++;
    tty_move_cursor_left(tty->input_len - tty->input_cursor);
}

static void tty_backspace_input(struct tty_device *tty) {
    if (tty->input_cursor == 0) {
        return;
    }

    tty->input_cursor--;
    for (size_t i = tty->input_cursor; i + 1 < tty->input_len; i++) {
        tty->input_buffer[i] = tty->input_buffer[i + 1];
    }
    tty->input_len--;

    tty_gui_cursor_left();
    tty_redraw_from_cursor(tty, tty->input_cursor);
    tty_write(NULL, " ", 1);
    tty_move_cursor_left(tty->input_len - tty->input_cursor + 1);
}

static void tty_commit_input_line(int minor, struct tty_device *tty, char terminator) {
    for (size_t i = 0; i < tty->input_len; i++) {
        if (!produce_ring_buffer(&tty->rx, &tty->input_buffer[i])) {
            return;
        }
    }

    if (!produce_ring_buffer(&tty->rx, &terminator)) {
        return;
    }

    tty->input_len = 0;
    tty->input_cursor = 0;
    wake_up_readers(minor);
}

static void tty_clear_input_line(struct tty_device *tty) {
    tty->input_len = 0;
    tty->input_cursor = 0;
}

static void tty_handle_arrow(struct tty_device *tty, char code) {
    if (code == 'D' && tty->input_cursor > 0) {
        tty->input_cursor--;
        tty_gui_cursor_left();
    } else if (code == 'C' && tty->input_cursor < tty->input_len) {
        tty->input_cursor++;
        tty_move_cursor_right(1);
    }
}

static void tty_switch_terminal_delta(int delta) {
    if (tty_state.num_ttys <= 1) {
        return;
    }

    int active = tty_gui_get_active_terminal();
    int next = tty_gui_next_visible_terminal(active, delta);
    if (next >= 0) {
        tty_gui_activate_terminal(next);
    }
}

static void tty_reset_escape(struct tty_device *tty) {
    tty->escape_state = 0;
}

static int tty_handle_escape_char(struct tty_device *tty, char ch) {
    if (tty->escape_state == 1) {
        tty->escape_state = (ch == '[' || ch == 'O') ? 2 : 0;
        return 1;
    }

    if (tty->escape_state != 2) {
        return 0;
    }

    if (ch == 'C' || ch == 'D') {
        tty_handle_arrow(tty, ch);
        tty_reset_escape(tty);
        return 1;
    }

    if (ch >= '0' && ch <= '9') {
        return 1;
    }

    if (ch == ';' || ch == ':') {
        return 1;
    }

    tty_reset_escape(tty);
    return 1;
}

void tty_send_input(int minor, const char *buffer, size_t count) {
    if (buffer == NULL) {
        return;
    }

    while (count > 0) {
        minor = tty_gui_get_active_terminal();
        if (tty_state.num_ttys <= minor) {
            return;
        }

        struct tty_device *tty = tty_state.devices[minor];
        if (tty == NULL) {
            return;
        }

        char ch = *buffer;

        if (ch == 0x14) {
            int restored = tty_show_hidden_terminal();
            if (restored >= 0) {
                buffer++;
                count--;
                continue;
            }

            if (tty_state.num_ttys + pending_terminal_creates < MAX_TTY_DEVICES) {
                pending_terminal_creates++;
                send_unblock_event(0, BLOCK_UNTIL_TTY_REQUEST);
            } else if (tty_state.num_ttys > 1) {
                tty_gui_activate_terminal(1);
            }
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x17) {
            tty_delete(minor);
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x0A) {
            tty_switch_terminal_delta(-1);
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x0B) {
            tty_switch_terminal_delta(1);
            buffer++;
            count--;
            continue;
        }

        if (tty->escape_state != 0) {
            tty_handle_escape_char(tty, ch);
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x1B) {
            tty->escape_state = 1;
            buffer++;
            count--;
            continue;
        }

        char to_write = ch;
        if (*buffer == 0x03) {
            s_kill(-tty_state.devices[minor]->fg_pgid, SIGINT);
            tty_write(NULL, "^C", 2);
            tty_clear_input_line(tty);
            to_write = 0x04;
        } else if (*buffer == 0x1A) {
            s_kill(-tty_state.devices[minor]->fg_pgid, SIGTSTP);
            tty_write(NULL, "^Z", 2);
            tty_clear_input_line(tty);
            to_write = 0;
        } else if (*buffer == 0x7F) {
            tty_backspace_input(tty);
            to_write = 0;
        } else if (*buffer == 0x0C) {
            tty_commit_input_line(minor, tty, 0x0C);
            to_write = 0;
        } else if (*buffer == 0x0D) {
            tty_move_cursor_right(tty->input_len - tty->input_cursor);
            tty_write(NULL, "\n", 1);
            tty_commit_input_line(minor, tty, '\n');
            to_write = 0;
        } else if (*buffer == 0x04) {
            if (tty->input_len == 0) {
                tty_commit_input_line(minor, tty, 0x04);
            }
            to_write = 0;
        } else {
            tty_insert_input_char(tty, ch);
            to_write = 0;
        }

        if (to_write) {
            if (!produce_ring_buffer(&tty_state.devices[minor]->rx, &to_write)) {
                return;
            }
        }

        if (to_write == 0x04 || to_write == 0x0C) {
            wake_up_readers(minor);
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

    if (tty_state.devices[fd]->fg_pgid != pcb->pgid && pgid != pcb->pgid) {
        s_kill(-pcb->pgid, SIGTTOU);
    }
    tty_state.devices[fd]->fg_pgid = pgid;
    return 0;
}
