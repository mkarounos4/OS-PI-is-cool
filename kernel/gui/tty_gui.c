#include "gui.h"
#include "tty_gui.h"
#include "ascii_font.h"
#include "string.h"

int MAX_ROWS;
int MAX_COLS;
#define MAIN_COLOR (uint32_t) gui_framebuffer_encode_color(0xFF, 0, 0xFF)
#define BG_COLOR (uint32_t) gui_framebuffer_encode_color(0x18, 0, 0x32)
#define CHAR_HEIGHT 16
#define CHAR_WIDTH 8
#define WIDTH_BUFFER 0
#define HEIGHT_BUFFER 0
#define TAB_BAR_HEIGHT 24
#define TAB_WIDTH 96
#define TAB_PADDING_X 10
#define TAB_LABEL_Y 4
#define TTY_GUI_TERMINALS 2
#define TTY_GUI_MAX_ROWS 80
#define TTY_GUI_MAX_COLS 160
#define TAB_BAR_COLOR (uint32_t) gui_framebuffer_encode_color(0x2b, 0x2b, 0x3c)
#define TAB_ACTIVE_COLOR (uint32_t) gui_framebuffer_encode_color(0x18, 0, 0x32)
#define TAB_INACTIVE_COLOR (uint32_t) gui_framebuffer_encode_color(0x42, 0x42, 0x55)
#define TAB_BORDER_COLOR (uint32_t) gui_framebuffer_encode_color(0x7d, 0x7d, 0x96)
#define TAB_TEXT_COLOR (uint32_t) gui_framebuffer_encode_color(0xff, 0xff, 0xff)

typedef struct tty_gui_terminal_st {
    int cursor_row;
    int cursor_col;
    char cells[TTY_GUI_MAX_ROWS][TTY_GUI_MAX_COLS];
} tty_gui_terminal_t;

static tty_gui_terminal_t terminals[TTY_GUI_TERMINALS];
static int active_terminal;

static uint32_t row_to_px(int row) {
    return TAB_BAR_HEIGHT + HEIGHT_BUFFER + row * (CHAR_HEIGHT + HEIGHT_BUFFER);
}

static uint32_t col_to_px(int col) {
    return WIDTH_BUFFER + col * (CHAR_WIDTH + WIDTH_BUFFER);
}

static tty_gui_terminal_t *terminal_for_index(int terminal) {
    if (terminal < 0 || terminal >= TTY_GUI_TERMINALS) {
        terminal = active_terminal;
    }

    return &terminals[terminal];
}

static tty_gui_terminal_t *active_state(void) {
    return &terminals[active_terminal];
}

static void draw_char_pixels(uint32_t x, uint32_t y, char c,
                             uint32_t fg, uint32_t bg) {
    uint8_t *char_format = get_ascii_char_font(c);
    for (int i = 0; i < CHAR_WIDTH; i++) {
        for (int j = 0; j < CHAR_HEIGHT; j++) {
            gui_framebuffer_put_pixel_encoded(x + (uint32_t)i,
                                              y + (uint32_t)j,
                                              (char_format[j] & (1 << (7 - i))) ?
                                                  fg : bg);
        }
    }
}

static void draw_char_at(int row, int col, char c) {
    draw_char_pixels(col_to_px(col), row_to_px(row), c, MAIN_COLOR, BG_COLOR);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    for (uint32_t row = y; row < y + h; row++) {
        for (uint32_t col = x; col < x + w; col++) {
            gui_framebuffer_put_pixel_encoded(col, row, color);
        }
    }
}

static void draw_tab_label(int tab, uint32_t x, uint32_t y, uint32_t bg) {
    char label[] = "tty0";
    label[3] = (char)('0' + tab);

    for (int i = 0; label[i] != '\0'; i++) {
        draw_char_pixels(x + (uint32_t)(i * CHAR_WIDTH), y, label[i],
                         TAB_TEXT_COLOR, bg);
    }
}

static void draw_tab_bar(void) {
    gui_framebuffer_t fb;
    if (!gui_framebuffer_get(&fb)) {
        return;
    }

    fill_rect(0, 0, fb.width, TAB_BAR_HEIGHT, TAB_BAR_COLOR);
    for (int tab = 0; tab < TTY_GUI_TERMINALS; tab++) {
        uint32_t x = (uint32_t)(tab * TAB_WIDTH);
        if (x >= fb.width) {
            break;
        }
        uint32_t tab_width = TAB_WIDTH;
        if (x + tab_width > fb.width) {
            tab_width = fb.width - x;
        }

        uint32_t color = tab == active_terminal ?
            TAB_ACTIVE_COLOR : TAB_INACTIVE_COLOR;
        fill_rect(x, 1, tab_width, TAB_BAR_HEIGHT - 1, color);
        fill_rect(x, TAB_BAR_HEIGHT - 1, tab_width, 1, TAB_BORDER_COLOR);
        fill_rect(x, 1, 1, TAB_BAR_HEIGHT - 1, TAB_BORDER_COLOR);
        fill_rect(x + tab_width - 1, 1, 1, TAB_BAR_HEIGHT - 1, TAB_BORDER_COLOR);
        draw_tab_label(tab, x + TAB_PADDING_X, TAB_LABEL_Y, color);
    }
}

static void redraw_active_terminal(void) {
    tty_gui_terminal_t *tty = active_state();

    gui_framebuffer_clear(0x18, 0, 0x32);
    draw_tab_bar();
    for (int row = 0; row < MAX_ROWS; row++) {
        for (int col = 0; col < MAX_COLS; col++) {
            char c = tty->cells[row][col];
            if (c != ' ' && c != '\0') {
                draw_char_at(row, col, c);
            }
        }
    }
}

static void scroll_down_row(tty_gui_terminal_t *tty) {
    for (int row = 1; row < MAX_ROWS; row++) {
        memcpy(tty->cells[row - 1], tty->cells[row], (size_t)MAX_COLS);
    }
    memset(tty->cells[MAX_ROWS - 1], ' ', (size_t)MAX_COLS);

    if (tty == active_state()) {
        redraw_active_terminal();
    }
}

void init_tty_gui(void) {
    gui_framebuffer_t fb;
    int success = gui_framebuffer_get(&fb);
    if (!success) {
        return;
    }

    MAX_COLS = fb.width / (CHAR_WIDTH + WIDTH_BUFFER);
    MAX_ROWS = (fb.height - TAB_BAR_HEIGHT) / (CHAR_HEIGHT + HEIGHT_BUFFER);
    if (MAX_COLS > TTY_GUI_MAX_COLS) {
        MAX_COLS = TTY_GUI_MAX_COLS;
    }
    if (MAX_ROWS > TTY_GUI_MAX_ROWS) {
        MAX_ROWS = TTY_GUI_MAX_ROWS;
    }

    active_terminal = 0;
    for (int i = 0; i < TTY_GUI_TERMINALS; i++) {
        terminals[i].cursor_row = 0;
        terminals[i].cursor_col = 0;
        memset(terminals[i].cells, ' ', sizeof(terminals[i].cells));
    }

    gui_framebuffer_clear(0x18, 0, 0x32);
    draw_tab_bar();
}

void toggle_cursor(void) {
    tty_gui_terminal_t *tty = active_state();

    for (uint32_t i = row_to_px(tty->cursor_row); i < row_to_px(tty->cursor_row) + 16; i++) {
        for (uint32_t j = col_to_px(tty->cursor_col); j < col_to_px(tty->cursor_col) + 8; j++) {
            uint32_t curr_color;
            int success = gui_framebuffer_get_pixel(j, i, &curr_color);
            if (!success) {
                return;
            }

            success = gui_framebuffer_put_pixel_encoded(j, i, curr_color == MAIN_COLOR ? BG_COLOR : MAIN_COLOR);
            if (!success) {
                return;
            }
        }
    }
}

static void cursor_advance_row(tty_gui_terminal_t *tty) {
    if (tty == active_state()) {
        toggle_cursor();
    }

    if (tty->cursor_row == MAX_ROWS - 1) {
        scroll_down_row(tty);
        tty->cursor_col = 0;
    } else {
        tty->cursor_col = 0;
        tty->cursor_row++;
    }

    if (tty == active_state()) {
        toggle_cursor();
    }
}

static void advance_cursor(tty_gui_terminal_t *tty) {
    tty->cursor_col++;
    if (tty->cursor_col == MAX_COLS) {
        tty->cursor_col = 0;
        tty->cursor_row++;
        
        if (tty->cursor_row == MAX_ROWS) {
            scroll_down_row(tty);
            tty->cursor_row--;
        }
    }
}

void tty_gui_cursor_left(void) {
    tty_gui_terminal_t *tty = active_state();

    if (tty->cursor_row == 0 && tty->cursor_col == 0) {
        return;
    }

    toggle_cursor();
    if (tty->cursor_col == 0) {
        tty->cursor_row--;
        tty->cursor_col = MAX_COLS - 1;
    } else {
        tty->cursor_col--;
    }
    toggle_cursor();
}

void tty_gui_cursor_right(void) {
    tty_gui_terminal_t *tty = active_state();

    if (tty->cursor_row == MAX_ROWS - 1 && tty->cursor_col == MAX_COLS - 1) {
        return;
    }

    toggle_cursor();
    tty->cursor_col++;
    if (tty->cursor_col == MAX_COLS) {
        tty->cursor_col = 0;
        tty->cursor_row++;
    }
    toggle_cursor();
}

void backtrack_cursor(void) {
    tty_gui_cursor_left();
}

static void clear_screen(void) {
    tty_gui_terminal_t *tty = active_state();

    tty->cursor_row = 0;
    tty->cursor_col = 0;
    memset(tty->cells, ' ', sizeof(tty->cells));
    redraw_active_terminal();
    toggle_cursor();
}

static void write_tab(void) {
    tty_gui_terminal_t *tty = active_state();
    int target_col = ((tty->cursor_col / 8) + 1) * 8;
    do {
        tty_gui_write_char(' ');
    } while (tty->cursor_col != 0 && tty->cursor_col < target_col);
}

void tty_gui_write_char(const char c) {
    tty_gui_write_char_for_tty(active_terminal, c);
}

void tty_gui_write_char_for_tty(int terminal, const char c) {
    tty_gui_terminal_t *tty = terminal_for_index(terminal);
    int is_active = tty == active_state();

    if (c == '\n') {
        cursor_advance_row(tty);
    } else if (c == '\b') {
        if (is_active) {
            backtrack_cursor();
        } else if (tty->cursor_row != 0 || tty->cursor_col != 0) {
            if (tty->cursor_col == 0) {
                tty->cursor_row--;
                tty->cursor_col = MAX_COLS - 1;
            } else {
                tty->cursor_col--;
            }
        }
    } else if (c == '\f') {
        if (is_active) {
            clear_screen();
        } else {
            tty->cursor_row = 0;
            tty->cursor_col = 0;
            memset(tty->cells, ' ', sizeof(tty->cells));
        }
    } else if (c == '\t') {
        if (is_active) {
            write_tab();
        }
    } else {
        if (is_active) {
            toggle_cursor();
        }

        tty->cells[tty->cursor_row][tty->cursor_col] = c;
        if (is_active) {
            draw_char_at(tty->cursor_row, tty->cursor_col, c);
        }
        advance_cursor(tty);

        if (is_active) {
            toggle_cursor();
        }
    }
}

void tty_gui_activate_terminal(int terminal) {
    if (terminal < 0 || terminal >= TTY_GUI_TERMINALS ||
        terminal == active_terminal) {
        return;
    }

    active_terminal = terminal;
    redraw_active_terminal();
    toggle_cursor();
}

int tty_gui_get_active_terminal(void) {
    return active_terminal;
}

int tty_gui_get_rows(void) {
    return MAX_ROWS;
}

int tty_gui_get_cols(void) {
    return MAX_COLS;
}

int tty_gui_get_cursor_row(void) {
    return active_state()->cursor_row;
}

int tty_gui_get_cursor_col(void) {
    return active_state()->cursor_col;
}
