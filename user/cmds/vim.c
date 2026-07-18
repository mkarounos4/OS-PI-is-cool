#include <stddef.h>

#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/malloc.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/tty_syscall.h"

#define VIM_SCREEN_MAX_CELLS (80 * 160)
#define VIM_COMMAND_MAX 128

enum editor_mode {
    EDITOR_NORMAL,
    EDITOR_INSERT,
    EDITOR_COMMAND,
};

static char *file_buffer;
static size_t file_length;
static size_t file_capacity;
static size_t cursor;
static int screen_rows;
static int screen_cols;
static int top_visual_row;
static int dirty;
static const char *file_path;
static char screen_cells[VIM_SCREEN_MAX_CELLS];
static char command_buffer[VIM_COMMAND_MAX];
static size_t command_length;
static char status_buffer[VIM_COMMAND_MAX];

static void set_status(const char *message) {
    size_t i = 0;
    while (i + 1 < sizeof(status_buffer) && message[i] != '\0') {
        status_buffer[i] = message[i];
        i++;
    }
    status_buffer[i] = '\0';
}

static int ensure_capacity(size_t needed) {
    if (needed <= file_capacity) {
        return 0;
    }

    size_t capacity = file_capacity == 0 ? 256 : file_capacity;
    while (capacity < needed) {
        capacity *= 2;
    }

    char *new_buffer = realloc(file_buffer, capacity);
    if (new_buffer == NULL) {
        return -ENOMEM;
    }
    file_buffer = new_buffer;
    file_capacity = capacity;
    return 0;
}

static int load_file(const char *path) {
    struct fs_stat_st metadata;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (fd == -ENOENT) {
            file_buffer = NULL;
            file_length = 0;
            file_capacity = 0;
            return 0;
        }
        print_errno("vim", path, fd);
        return fd;
    }

    int stat_err = stat(path, &metadata);
    if (stat_err < 0) {
        close(fd);
        print_errno("vim", path, stat_err);
        return stat_err;
    }
    if (ensure_capacity((size_t)metadata.size + 1) != 0) {
        close(fd);
        print_errno("vim", path, -ENOMEM);
        return -ENOMEM;
    }

    file_length = 0;
    while (file_length < metadata.size) {
        int count = read(fd, file_buffer + file_length,
                         (int)(metadata.size - file_length));
        if (count < 0) {
            close(fd);
            print_errno("vim", path, count);
            return count;
        }
        if (count == 0) {
            break;
        }
        file_length += (size_t)count;
    }
    close(fd);
    return 0;
}

static int write_all(int fd, const char *buffer, size_t length) {
    size_t written = 0;
    while (written < length) {
        int count = write(fd, buffer + written, (int)(length - written));
        if (count <= 0) {
            return count < 0 ? count : -EIO;
        }
        written += (size_t)count;
    }
    return 0;
}

static int save_file(void) {
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        print_errno("vim", file_path, fd);
        return fd;
    }

    int err = write_all(fd, file_buffer, file_length);
    int close_err = close(fd);
    if (err == 0 && close_err < 0) {
        err = close_err;
    }
    if (err < 0) {
        print_errno("vim", file_path, err);
        return err;
    }

    dirty = 0;
    set_status("written");
    return 0;
}

static int insert_bytes(const char *bytes, size_t length) {
    if (ensure_capacity(file_length + length + 1) != 0) {
        set_status("out of memory");
        return -ENOMEM;
    }

    for (size_t i = file_length; i > cursor; i--) {
        file_buffer[i + length - 1] = file_buffer[i - 1];
    }
    for (size_t i = 0; i < length; i++) {
        file_buffer[cursor + i] = bytes[i];
    }
    cursor += length;
    file_length += length;
    file_buffer[file_length] = '\0';
    dirty = 1;
    return 0;
}

static void delete_at_cursor(void) {
    if (cursor >= file_length) {
        return;
    }

    for (size_t i = cursor; i + 1 < file_length; i++) {
        file_buffer[i] = file_buffer[i + 1];
    }
    file_length--;
    file_buffer[file_length] = '\0';
    dirty = 1;
}

static void cursor_position(size_t index, int *row, int *col) {
    int visual_row = 0;
    int visual_col = 0;
    int wrapped = 0;

    for (size_t i = 0; i < index && i < file_length; i++) {
        if (file_buffer[i] == '\n') {
            if (!wrapped) {
                visual_row++;
            }
            visual_col = 0;
            wrapped = 0;
        } else {
            visual_col++;
            if (visual_col == screen_cols) {
                visual_row++;
                visual_col = 0;
                wrapped = 1;
            } else {
                wrapped = 0;
            }
        }
    }

    *row = visual_row;
    *col = visual_col;
}

static size_t index_at_position(int target_row, int target_col) {
    int visual_row = 0;
    int visual_col = 0;
    int wrapped = 0;

    for (size_t i = 0; i < file_length; i++) {
        if (visual_row == target_row && visual_col >= target_col) {
            return i;
        }

        if (file_buffer[i] == '\n') {
            if (!wrapped) {
                visual_row++;
            }
            visual_col = 0;
            wrapped = 0;
        } else {
            visual_col++;
            if (visual_col == screen_cols) {
                visual_row++;
                visual_col = 0;
                wrapped = 1;
            } else {
                wrapped = 0;
            }
        }
    }

    return file_length;
}

static void move_vertical(int delta) {
    int row;
    int col;
    cursor_position(cursor, &row, &col);
    int target_row = row + delta;
    if (target_row < 0) {
        target_row = 0;
    }

    size_t target = index_at_position(target_row, col);
    int actual_row;
    int actual_col;
    cursor_position(target, &actual_row, &actual_col);
    if (actual_row == target_row || target_row > actual_row) {
        cursor = target;
    }
}

static void ensure_cursor_visible(void) {
    int row;
    int col;
    cursor_position(cursor, &row, &col);
    (void)col;
    int content_rows = screen_rows > 1 ? screen_rows - 1 : 1;
    if (row < top_visual_row) {
        top_visual_row = row;
    } else if (row >= top_visual_row + content_rows) {
        top_visual_row = row - content_rows + 1;
    }
    if (top_visual_row < 0) {
        top_visual_row = 0;
    }
}

static void clear_screen_buffer(void) {
    size_t size = (size_t)screen_rows * (size_t)screen_cols;
    for (size_t i = 0; i < size; i++) {
        screen_cells[i] = ' ';
    }
}

static void draw_text(void) {
    int visual_row = 0;
    int visual_col = 0;
    int wrapped = 0;
    int content_rows = screen_rows > 1 ? screen_rows - 1 : 1;

    for (size_t i = 0; i < file_length; i++) {
        if (file_buffer[i] == '\n') {
            if (!wrapped) {
                visual_row++;
            }
            visual_col = 0;
            wrapped = 0;
            continue;
        }

        if (visual_row >= top_visual_row &&
            visual_row < top_visual_row + content_rows &&
            visual_col < screen_cols) {
            size_t screen_index = (size_t)(visual_row - top_visual_row) *
                                  (size_t)screen_cols + (size_t)visual_col;
            screen_cells[screen_index] = file_buffer[i];
        }

        visual_col++;
        if (visual_col == screen_cols) {
            visual_row++;
            visual_col = 0;
            wrapped = 1;
        } else {
            wrapped = 0;
        }
    }
}

static void draw_status(enum editor_mode mode) {
    int status_row = screen_rows - 1;
    size_t base = (size_t)status_row * (size_t)screen_cols;
    const char *mode_text = mode == EDITOR_INSERT ? "-- INSERT --" :
                            mode == EDITOR_COMMAND ? ":" : "-- NORMAL --";
    size_t pos = 0;

    while (pos < (size_t)screen_cols && mode_text[pos] != '\0') {
        screen_cells[base + pos] = mode_text[pos];
        pos++;
    }
    if (mode == EDITOR_COMMAND) {
        for (size_t i = 0; i < command_length && pos < (size_t)screen_cols; i++) {
            screen_cells[base + pos++] = command_buffer[i];
        }
    } else {
        const char *message = dirty ? " [+]" : " [-]";
        for (size_t i = 0; message[i] != '\0' && pos < (size_t)screen_cols; i++) {
            screen_cells[base + pos++] = message[i];
        }
        for (size_t i = 0; status_buffer[i] != '\0' && pos < (size_t)screen_cols; i++) {
            screen_cells[base + pos++] = status_buffer[i];
        }
    }
}

static int render(enum editor_mode mode) {
    clear_screen_buffer();
    ensure_cursor_visible();
    draw_text();
    draw_status(mode);

    int cursor_row;
    int cursor_col;
    if (mode == EDITOR_COMMAND) {
        cursor_row = screen_rows - 1;
        cursor_col = (int)(1 + command_length);
    } else {
        int visual_row;
        cursor_position(cursor, &visual_row, &cursor_col);
        cursor_row = visual_row - top_visual_row;
        if (cursor_row < 0) {
            cursor_row = 0;
        }
        if (cursor_row >= screen_rows - 1) {
            cursor_row = screen_rows - 2;
        }
    }

    if (cursor_col >= screen_cols) {
        cursor_col = screen_cols - 1;
    }
    return tty_screen_present(STDIN_FILENO, screen_cells,
                              (size_t)screen_rows * (size_t)screen_cols,
                              cursor_row, cursor_col);
}

static int command_is(const char *command) {
    return strcmp(command_buffer, command) == 0;
}

static int handle_command(int *quit) {
    if (command_is("q!")) {
        *quit = 1;
        return 0;
    }
    if (command_is("q")) {
        if (dirty) {
            set_status("no write since last change");
            return -1;
        }
        *quit = 1;
        return 0;
    }
    if (command_is("w") || command_is("wq")) {
        if (save_file() != 0) {
            return -1;
        }
        if (command_is("wq")) {
            *quit = 1;
        }
        return 0;
    }

    set_status("unknown command");
    return -1;
}

static int handle_normal_key(char ch, enum editor_mode *mode) {
    if (ch == 'h') {
        if (cursor > 0) {
            cursor--;
        }
    } else if (ch == 'l') {
        if (cursor < file_length) {
            cursor++;
        }
    } else if (ch == 'j') {
        move_vertical(1);
    } else if (ch == 'k') {
        move_vertical(-1);
    } else if (ch == 'x') {
        delete_at_cursor();
    } else if (ch == 'i') {
        *mode = EDITOR_INSERT;
    } else if (ch == ':') {
        command_length = 0;
        command_buffer[0] = '\0';
        *mode = EDITOR_COMMAND;
    }
    return 0;
}

static int handle_insert_key(char ch, enum editor_mode *mode) {
    if (ch == 0x1B) {
        *mode = EDITOR_NORMAL;
        return 0;
    }
    if (ch == 0x7F || ch == '\b') {
        if (cursor > 0) {
            cursor--;
            delete_at_cursor();
        }
        return 0;
    }
    if (ch == '\r' || ch == '\n') {
        return insert_bytes("\n", 1);
    }
    if (ch >= 0x20 && ch <= 0x7E) {
        return insert_bytes(&ch, 1);
    }
    return 0;
}

static int handle_command_key(char ch, enum editor_mode *mode, int *quit) {
    if (ch == 0x1B) {
        command_length = 0;
        command_buffer[0] = '\0';
        *mode = EDITOR_NORMAL;
        return 0;
    }
    if (ch == 0x7F || ch == '\b') {
        if (command_length > 0) {
            command_length--;
            command_buffer[command_length] = '\0';
        }
        return 0;
    }
    if (ch == '\r' || ch == '\n') {
        int result = handle_command(quit);
        if (!*quit) {
            *mode = EDITOR_NORMAL;
        }
        return result;
    }
    if (ch >= 0x20 && ch <= 0x7E && command_length + 1 < VIM_COMMAND_MAX) {
        command_buffer[command_length++] = ch;
        command_buffer[command_length] = '\0';
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2 || argv[1] == NULL) {
        print_errno("vim", "usage: vim <file>", -EINVAL);
        return -EINVAL;
    }

    file_path = argv[1];
    if (load_file(file_path) != 0) {
        free(file_buffer);
        return -EIO;
    }

    if (tty_screen_enter(STDIN_FILENO) < 0 ||
        tty_get_size(STDIN_FILENO, &screen_rows, &screen_cols) < 0 ||
        screen_rows < 2 || screen_cols < 1 ||
        (size_t)screen_rows * (size_t)screen_cols > VIM_SCREEN_MAX_CELLS ||
        tty_set_mode(STDIN_FILENO, TTY_MODE_RAW) < 0) {
        print_errno("vim", "terminal setup failed", -EIO);
        tty_screen_leave(STDIN_FILENO);
        free(file_buffer);
        return -EIO;
    }

    enum editor_mode mode = EDITOR_NORMAL;
    int quit = 0;
    set_status(file_path);
    render(mode);

    while (!quit) {
        char ch;
        int count = read(STDIN_FILENO, &ch, 1);
        if (count < 0) {
            set_status("input error");
            break;
        }
        if (count == 0) {
            continue;
        }

        if (mode == EDITOR_NORMAL) {
            handle_normal_key(ch, &mode);
        } else if (mode == EDITOR_INSERT) {
            handle_insert_key(ch, &mode);
        } else {
            handle_command_key(ch, &mode, &quit);
        }
        render(mode);
    }

    tty_set_mode(STDIN_FILENO, TTY_MODE_CANONICAL);
    tty_screen_leave(STDIN_FILENO);
    free(file_buffer);
    return 0;
}
