#include "tty.h"
#include "gui/tty_gui.h"
#include "scheduler.h"
#include "threading/thread.h"
#include "uart/uart.h"
#include "fs/kapi.h"
#include "fs/disk.h"
#include "gui/tty_gui_device.h"

static struct tty_driver_state tty_state;

int tty_open(struct oft_entry *entry);
int tty_close(struct oft_entry *entry);
int tty_read(struct oft_entry *entry, char *buffer, size_t count);
static void tty_clear_input_line(struct tty_device *tty);
void wake_up_readers(int minor);

static int tty_minor_from_fd(int fd) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL || fd < 0 || (size_t)fd >= vec_len(&pcb->file_descriptors)) {
        return -1;
    }

    int kernel_fd = (int)(uintptr_t)vec_get(&pcb->file_descriptors, fd);
    if (kernel_fd < 0) {
        return -1;
    }

    struct oft_entry *entry;
    if (get_oft_entry_by_fd(kernel_fd, &entry) != SUCCESS ||
        entry == NULL || entry->inode == NULL ||
        entry->inode->inode.metadata.type != CHAR_DRIVER_TYPE) {
        return -1;
    }

    return entry->inode->inode.metadata.i_rdev.minor;
}

static struct tty_device *tty_from_fd(int fd, int *minor_out) {
    int minor = tty_minor_from_fd(fd);
    if (minor < 0 || minor >= MAX_TTY_DEVICES ||
        tty_state.devices[minor] == NULL || !tty_state.devices[minor]->active) {
        return NULL;
    }

    if (minor_out != NULL) {
        *minor_out = minor;
    }
    return tty_state.devices[minor];
}

static int tty_session_owned_by_current(struct tty_device *tty) {
    pcb_t *pcb = get_curr_process();
    return pcb != NULL && tty->session_active &&
           tty->session_owner_pid == pcb->pid;
}

static void tty_session_free(struct tty_device *tty) {
    if (tty->session_saved_cells != NULL) {
        kfree(tty->session_saved_cells);
    }
    if (tty->session_suspended_cells != NULL) {
        kfree(tty->session_suspended_cells);
    }
    tty->session_saved_cells = NULL;
    tty->session_suspended_cells = NULL;
    tty->session_active = 0;
    tty->session_suspended = 0;
    tty->session_owner_pid = -1;
    tty->session_owner_pgid = -1;
}

static void tty_session_restore_shell(struct tty_device *tty) {
    if (!tty->session_active || tty->session_saved_cells == NULL) {
        return;
    }

    tty_gui_restore_screen((int)tty->minor, tty->session_saved_cells,
                           tty_gui_screen_size(), tty->session_saved_cursor_row,
                           tty->session_saved_cursor_col);
    tty->mode = tty->session_saved_mode;
    tty->fg_pgid = tty->session_saved_fg_pgid;
    if (tty->session_saved_active_terminal >= 0) {
        tty_gui_activate_terminal(tty->session_saved_active_terminal);
    }
}

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

    struct dev_st device_number = {
        .major = TTY_MAJOR,
        .minor = (uint16_t)minor,
    };
    err_t err = devfs_create_char_device(device_number);
    if (err) {
        return err;
    }

    if (tty_gui_create_terminal(minor) != 0) {
        return -2;
    }
    if (tty_gui_char_device_activate(minor) != 0) {
        tty_gui_destroy_terminal(minor);
        return -2;
    }

    struct tty_device *new_tty = kmalloc(sizeof(struct tty_device));
    if (new_tty == NULL) {
        tty_gui_char_device_deactivate(minor);
        tty_gui_destroy_terminal(minor);
        return -2;
    }
    kmemset(new_tty, 0, sizeof(struct tty_device));
    tty_state.devices[minor] = new_tty;
    new_tty->rx = create_ring_buffer(4096);
    new_tty->tx = create_ring_buffer(4096);
    new_tty->rx_wait_queue = vec_new(2, NULL);
    new_tty->tx_wait_queue = vec_new(2, NULL);

    new_tty->name[0] = 't';
    new_tty->name[1] = 't';
    new_tty->name[2] = 'y';
    new_tty->name[3] = '0' + minor;
    new_tty->name[4] = '\0';

    new_tty->device_number = (struct dev_st) {
        .major = TTY_MAJOR,
        .minor = minor,
    };
    new_tty->input_backend = (struct dev_st) {
        .major = UART_MAJOR,
        .minor = 0,
    };
#ifdef UART_OUT
    new_tty->output_backend = (struct dev_st) {
        .major = UART_MAJOR,
        .minor = 0,
    };
#else
    new_tty->output_backend = (struct dev_st) {
        .major = TTY_GUI_MAJOR,
        .minor = minor,
    };
#endif

    new_tty->fg_pgid = 0;
    new_tty->active = 0;
    new_tty->mode = TTY_MODE_CANONICAL;
    tty_session_free(new_tty);
    new_tty->input_len = 0;
    new_tty->input_cursor = 0;
    new_tty->escape_state = 0;
    new_tty->command_history_count = 0;
    new_tty->command_history_cursor = -1;
    new_tty->command_history_scratch_len = 0;

    new_tty->active = 1;
    tty_state.num_ttys++;
    return minor;
}

int tty_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return -1;
    }
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= MAX_TTY_DEVICES || tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active) {
        return -1;
    }
    tty_state.devices[minor]->refcount++;
    return 0;
}

int tty_close(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return -1;
    }
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
    if (entry == NULL || entry->inode == NULL ||
        (buffer == NULL && count != 0)) {
        return -1;
    }

    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;

    if (minor >= MAX_TTY_DEVICES) {
        return -1;
    }

    struct tty_device *curr_tty = tty_state.devices[minor];
    if (curr_tty == NULL || !curr_tty->active) {
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
    if (curr_thd == NULL) {
        return -1;
    }

    ssize_t num_read = 0;
    while (num_read < count) {
        char char_void;
        bool read_char = consume_ring_buffer(&curr_tty->rx, &char_void);
        if (!read_char) {
            int already_waiting = 0;
            for (size_t i = 0; i < vec_len(&curr_tty->rx_wait_queue); i++) {
                if ((tid_t)(uintptr_t)vec_get(&curr_tty->rx_wait_queue, i) ==
                    curr_thd->tid) {
                    already_waiting = 1;
                    break;
                }
            }
            if (!already_waiting) {
                vec_push_back(&curr_tty->rx_wait_queue,
                              (ptr_t)(uintptr_t)curr_thd->tid);
            }
            block_thread(curr_thd, THREAD_BLOCKED_INTERRUPTABLE);
            read_char = consume_ring_buffer(&curr_tty->rx, &char_void);
            if (!read_char) {
                return num_read;
            }
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

int tty_set_mode(int fd, int mode) {
    struct tty_device *tty = tty_from_fd(fd, NULL);
    if (tty == NULL || (mode != TTY_MODE_CANONICAL && mode != TTY_MODE_RAW)) {
        return -1;
    }
    if (tty->session_active && !tty_session_owned_by_current(tty)) {
        return -1;
    }

    tty->mode = (uint8_t)mode;
    if (tty_session_owned_by_current(tty) && !tty->session_suspended) {
        tty->session_resume_mode = (uint8_t)mode;
    }
    return 0;
}

int tty_get_mode(int fd) {
    struct tty_device *tty = tty_from_fd(fd, NULL);
    return tty == NULL ? -1 : tty->mode;
}

int tty_get_screen_size(int fd, int *rows, int *cols) {
    if (tty_from_fd(fd, NULL) == NULL || rows == NULL || cols == NULL) {
        return -1;
    }
    *rows = tty_gui_get_rows();
    *cols = tty_gui_get_cols();
    return (*rows > 0 && *cols > 0) ? 0 : -1;
}

int tty_screen_enter(int fd) {
    int minor;
    struct tty_device *tty = tty_from_fd(fd, &minor);
    pcb_t *pcb = get_curr_process();
    if (tty == NULL || pcb == NULL || tty->session_active) {
        return -1;
    }

    size_t screen_size = tty_gui_screen_size();
    tty->session_saved_cells = kmalloc(screen_size);
    tty->session_suspended_cells = kmalloc(screen_size);
    if (tty->session_saved_cells == NULL || tty->session_suspended_cells == NULL ||
        tty_gui_copy_screen(minor, tty->session_saved_cells, screen_size,
                            &tty->session_saved_cursor_row,
                            &tty->session_saved_cursor_col) != 0) {
        tty_session_free(tty);
        return -1;
    }

    tty->session_active = 1;
    tty->session_suspended = 0;
    tty->session_owner_pid = pcb->pid;
    tty->session_owner_pgid = pcb->pgid;
    tty->session_saved_fg_pgid = tty->fg_pgid;
    tty->session_saved_mode = tty->mode;
    tty->session_resume_mode = tty->mode;
    tty->session_saved_active_terminal = tty_gui_get_active_terminal();
    tty->fg_pgid = pcb->pgid;
    tty_gui_activate_terminal(minor);
    return 0;
}

int tty_screen_leave(int fd) {
    struct tty_device *tty = tty_from_fd(fd, NULL);
    if (tty == NULL || !tty_session_owned_by_current(tty)) {
        return -1;
    }

    tty_session_restore_shell(tty);
    tty_session_free(tty);
    return 0;
}

int tty_screen_present(int fd, const char *cells, size_t count,
                       int cursor_row, int cursor_col) {
    int minor;
    struct tty_device *tty = tty_from_fd(fd, &minor);
    if (tty == NULL || !tty_session_owned_by_current(tty) ||
        tty->session_suspended) {
        return -1;
    }

    return tty_gui_present_screen(minor, cells, count, cursor_row, cursor_col);
}

void tty_session_process_exit(pid_t pid) {
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        struct tty_device *tty = tty_state.devices[i];
        if (tty == NULL || !tty->active || !tty->session_active ||
            tty->session_owner_pid != pid) {
            continue;
        }

        tty_session_restore_shell(tty);
        tty_session_free(tty);
    }
}

void tty_session_process_stop(pid_t pid) {
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        struct tty_device *tty = tty_state.devices[i];
        if (tty == NULL || !tty->active || !tty->session_active ||
            tty->session_owner_pid != pid || tty->session_suspended) {
            continue;
        }

        if (tty_gui_copy_screen((int)tty->minor, tty->session_suspended_cells,
                                tty_gui_screen_size(),
                                &tty->session_suspended_cursor_row,
                                &tty->session_suspended_cursor_col) != 0) {
            continue;
        }
        tty->session_suspended = 1;
        tty_session_restore_shell(tty);
    }
}

void tty_session_process_continue(pid_t pid) {
    for (int i = 0; i < MAX_TTY_DEVICES; i++) {
        struct tty_device *tty = tty_state.devices[i];
        if (tty == NULL || !tty->active || !tty->session_active ||
            tty->session_owner_pid != pid || !tty->session_suspended ||
            tty->fg_pgid != tty->session_owner_pgid) {
            continue;
        }

        tty_gui_activate_terminal((int)tty->minor);
        tty_gui_present_screen((int)tty->minor, tty->session_suspended_cells,
                               tty_gui_screen_size(),
                               tty->session_suspended_cursor_row,
                               tty->session_suspended_cursor_col);
        tty->mode = tty->session_resume_mode;
        tty->fg_pgid = tty->session_owner_pgid;
        tty->session_suspended = 0;
    }
}

static const char *tty_backend_name(struct dev_st backend) {
    switch (backend.major) {
    case TTY_MAJOR:
        return "tty";
    case UART_MAJOR:
        return "uart";
    case TTY_GUI_MAJOR:
        return "ttygui";
    default:
        return "unknown";
    }
}

int tty_write(struct oft_entry *entry, const char *buffer, size_t count) {
    if (buffer == NULL && count != 0) {
        return -1;
    }

    uint16_t minor = (uint16_t)tty_gui_get_active_terminal();
    if (entry != NULL) {
        if (entry->inode == NULL) {
            return -1;
        }
        minor = entry->inode->inode.metadata.i_rdev.minor;
    }
    if (minor >= MAX_TTY_DEVICES || tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active) {
        return 0;
    }

    return char_device_write(tty_state.devices[minor]->output_backend, buffer, count);
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
                           "input_backend: %s%u (%u:%u)\n"
                           "output_backend: %s%u (%u:%u)\n"
                           "rows: %d\n"
                           "cols: %d\n"
                           "cursor: %d,%d\n"
                           "input_buffer: %d\n"
                           "output_buffer: %d\n"
                           "refcount: %d\n"
                           "canonical_mode: yes\n",
                           tty->name,
                           tty->fg_pgid,
                           tty_backend_name(tty->input_backend),
                           tty->input_backend.minor,
                           tty->input_backend.major,
                           tty->input_backend.minor,
                           tty_backend_name(tty->output_backend),
                           tty->output_backend.minor,
                           tty->output_backend.major,
                           tty->output_backend.minor,
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
    if (minor < 0 || minor >= MAX_TTY_DEVICES ||
        tty_state.devices[minor] == NULL) {
        return;
    }

    while (!vec_is_empty(&tty_state.devices[minor]->rx_wait_queue)) {
        void *tid;
        int removed = (int)(uintptr_t)vec_pop_back(&tty_state.devices[minor]->rx_wait_queue, &tid);
        if (!removed) {
            continue;
        }
        tcb_t *thread = thread_get_by_tid((tid_t)(uintptr_t)tid);
        if (thread != NULL) {
            unblock_thread(thread);
        }
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

static void tty_destroy_device(int minor) {
    struct tty_device *tty = tty_state.devices[minor];
    if (tty == NULL) {
        return;
    }

    tty->active = 0;
    wake_up_readers(minor);
    tty_session_free(tty);
    destroy_ring_buffer(&tty->rx);
    destroy_ring_buffer(&tty->tx);
    vec_destroy(&tty->rx_wait_queue);
    vec_destroy(&tty->tx_wait_queue);
    kfree(tty);
    tty_state.devices[minor] = NULL;
    if (tty_state.num_ttys > 0) {
        tty_state.num_ttys--;
    }
}

int tty_delete(int minor) {
    if (minor < 0 || minor >= MAX_TTY_DEVICES ||
        tty_state.devices[minor] == NULL ||
        !tty_state.devices[minor]->active ||
        !tty_gui_is_terminal_visible(minor) ||
        tty_visible_count() <= 1) {
        return -1;
    }

    tty_gui_destroy_terminal(minor);
    tty_gui_char_device_deactivate(minor);
    tty_destroy_device(minor);
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

static int tty_lines_equal(const char *lhs, size_t lhs_len,
                           const char *rhs, size_t rhs_len) {
    if (lhs_len != rhs_len) {
        return 0;
    }
    for (size_t i = 0; i < lhs_len; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static void tty_history_reset_navigation(struct tty_device *tty) {
    tty->command_history_cursor = -1;
    tty->command_history_scratch_len = 0;
}

static void tty_history_push(struct tty_device *tty) {
    if (tty->input_len == 0) {
        tty_history_reset_navigation(tty);
        return;
    }

    size_t len = tty->input_len;
    if (len >= TTY_COMMAND_HISTORY_LINE_SIZE) {
        len = TTY_COMMAND_HISTORY_LINE_SIZE - 1;
    }

    if (tty->command_history_count > 0) {
        int newest = tty->command_history_count - 1;
        if (tty_lines_equal(tty->command_history[newest],
                            tty->command_history_len[newest],
                            tty->input_buffer, len)) {
            tty_history_reset_navigation(tty);
            return;
        }
    }

    if (tty->command_history_count == TTY_COMMAND_HISTORY_DEPTH) {
        for (int i = 1; i < TTY_COMMAND_HISTORY_DEPTH; i++) {
            for (size_t j = 0; j < TTY_COMMAND_HISTORY_LINE_SIZE; j++) {
                tty->command_history[i - 1][j] = tty->command_history[i][j];
            }
            tty->command_history_len[i - 1] = tty->command_history_len[i];
        }
        tty->command_history_count--;
    }

    int slot = tty->command_history_count++;
    for (size_t i = 0; i < len; i++) {
        tty->command_history[slot][i] = tty->input_buffer[i];
    }
    tty->command_history[slot][len] = '\0';
    tty->command_history_len[slot] = len;
    tty_history_reset_navigation(tty);
}

static void tty_redraw_from_cursor(struct tty_device *tty, size_t cursor) {
    for (size_t i = cursor; i < tty->input_len; i++) {
        tty_write(NULL, &tty->input_buffer[i], 1);
    }
}

static void tty_replace_input_line(struct tty_device *tty, const char *line,
                                   size_t len) {
    size_t old_len = tty->input_len;
    if (len >= TTY_INPUT_BUFFER_SIZE) {
        len = TTY_INPUT_BUFFER_SIZE - 1;
    }

    tty_move_cursor_left(tty->input_cursor);
    for (size_t i = 0; i < len; i++) {
        tty->input_buffer[i] = line[i];
    }
    tty->input_len = len;
    tty->input_cursor = len;
    tty_redraw_from_cursor(tty, 0);

    if (old_len > len) {
        size_t trailing = old_len - len;
        for (size_t i = 0; i < trailing; i++) {
            tty_write(NULL, " ", 1);
        }
        tty_move_cursor_left(trailing);
    }
}

static void tty_history_recall(struct tty_device *tty, int direction) {
    if (tty->command_history_count == 0) {
        return;
    }

    if (tty->command_history_cursor < 0) {
        if (direction > 0) {
            return;
        }
        tty->command_history_scratch_len = tty->input_len;
        for (size_t i = 0; i < tty->input_len; i++) {
            tty->command_history_scratch[i] = tty->input_buffer[i];
        }
        tty->command_history_cursor = tty->command_history_count - 1;
    } else if (direction < 0 && tty->command_history_cursor > 0) {
        tty->command_history_cursor--;
    } else if (direction > 0) {
        if (tty->command_history_cursor < tty->command_history_count - 1) {
            tty->command_history_cursor++;
        } else {
            tty_replace_input_line(tty, tty->command_history_scratch,
                                   tty->command_history_scratch_len);
            tty_history_reset_navigation(tty);
            return;
        }
    }

    int slot = tty->command_history_cursor;
    tty_replace_input_line(tty, tty->command_history[slot],
                           tty->command_history_len[slot]);
}

static void tty_insert_input_char(struct tty_device *tty, char ch) {
    if (tty->input_len + 1 >= TTY_INPUT_BUFFER_SIZE) {
        return;
    }
    tty_history_reset_navigation(tty);

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
    tty_history_reset_navigation(tty);

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
    if (terminator == '\n') {
        tty_history_push(tty);
    } else {
        tty_history_reset_navigation(tty);
    }

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
    tty_history_reset_navigation(tty);
}

static void tty_handle_arrow(struct tty_device *tty, char code) {
    if (code == 'A') {
        tty_history_recall(tty, -1);
    } else if (code == 'B') {
        tty_history_recall(tty, 1);
    } else if (code == 'D' && tty->input_cursor > 0) {
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

    if (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D') {
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

static void tty_send_raw_char(int minor, struct tty_device *tty, char ch) {
    if (ch == 0x03) {
        if (tty->fg_pgid > 0) {
            s_kill(-tty->fg_pgid, SIGINT);
        }
        return;
    }

    if (ch == 0x1A) {
        if (tty->fg_pgid > 0) {
            s_kill(-tty->fg_pgid, SIGTSTP);
        }
        return;
    }

    if (produce_ring_buffer(&tty->rx, &ch)) {
        wake_up_readers(minor);
    }
}

static int tty_backend_matches(struct dev_st a, struct dev_st b) {
    return a.major == b.major && a.minor == b.minor;
}

static int tty_input_target_for_backend(struct dev_st input_backend) {
    int active = tty_gui_get_active_terminal();
    if (active >= 0 && active < MAX_TTY_DEVICES &&
        tty_state.devices[active] != NULL &&
        tty_state.devices[active]->active &&
        tty_backend_matches(tty_state.devices[active]->input_backend, input_backend)) {
        return active;
    }

    for (int minor = 0; minor < MAX_TTY_DEVICES; minor++) {
        if (tty_state.devices[minor] != NULL &&
            tty_state.devices[minor]->active &&
            tty_backend_matches(tty_state.devices[minor]->input_backend, input_backend)) {
            return minor;
        }
    }

    return -1;
}

void tty_receive_input_from_device(struct dev_st input_backend, size_t count) {
    char buffer[64];
    size_t remaining = count;

    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        int bytes_read = char_device_read(input_backend, buffer, chunk);
        if (bytes_read <= 0) {
            return;
        }

        for (int i = 0; i < bytes_read; i++) {
            int target_minor = tty_input_target_for_backend(input_backend);
            if (target_minor < 0) {
                return;
            }
            tty_send_input(target_minor, &buffer[i], 1);
        }
        if ((size_t)bytes_read < chunk) {
            return;
        }
        remaining -= (size_t)bytes_read;
    }
}

void tty_send_input(int minor, const char *buffer, size_t count) {
    if (buffer == NULL) {
        return;
    }

    while (count > 0) {
        if (minor < 0 || minor >= MAX_TTY_DEVICES ||
            tty_state.devices[minor] == NULL ||
            !tty_state.devices[minor]->active) {
            return;
        }

        struct tty_device *tty = tty_state.devices[minor];

        char ch = *buffer;

        if (tty->mode == TTY_MODE_RAW) {
            tty_send_raw_char(minor, tty, ch);
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x14) {
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

        if (ch == 0x18) {
            tty_history_recall(tty, -1);
            buffer++;
            count--;
            continue;
        }

        if (ch == 0x19) {
            tty_history_recall(tty, 1);
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
            if (tty->fg_pgid > 0) {
                s_kill(-tty->fg_pgid, SIGINT);
            }
            tty_write(NULL, "^C\n", 3);
            tty_clear_input_line(tty);
            to_write = 0;
        } else if (*buffer == 0x1A) {
            if (tty->fg_pgid > 0) {
                s_kill(-tty->fg_pgid, SIGTSTP);
            }
            tty_write(NULL, "^Z\n", 3);
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
            if (!produce_ring_buffer(&tty->rx, &to_write)) {
                return;
            }
        }

        if (ch == 0x04 || ch == 0x0C) {
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
    struct tty_device *tty = tty_from_fd(fd, NULL);
    if (tty == NULL) {
        return -1;
    }
    if (pgid == 0) {
        pgid = pcb->pgid;
    }

    if (tty->fg_pgid != pcb->pgid && pgid != pcb->pgid) {
        s_kill(-pcb->pgid, SIGTTOU);
    }
    tty->fg_pgid = pgid;
    return 0;
}
