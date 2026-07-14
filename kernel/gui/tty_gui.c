#include "gui.h"
#include "tty_gui.h"
#include "ascii_font.h"

int MAX_ROWS;
int MAX_COLS;
static int cursor_row;
static int cursor_col;
#define MAIN_COLOR (uint32_t) gui_framebuffer_encode_color(0xFF, 0, 0xFF)
#define BG_COLOR (uint32_t) gui_framebuffer_encode_color(0x18, 0, 0x32)
#define CHAR_HEIGHT 16
#define CHAR_WIDTH 8
#define WIDTH_BUFFER 0
#define HEIGHT_BUFFER 0

static uint32_t row_to_px(int row) {
    return HEIGHT_BUFFER + row * (CHAR_HEIGHT + HEIGHT_BUFFER);
}

static uint32_t col_to_px(int col) {
    return WIDTH_BUFFER + col * (CHAR_WIDTH + WIDTH_BUFFER);
}

static void scroll_down_row(void) {
    // Shift everything up
    gui_framebuffer_t fb;
    int success = gui_framebuffer_get(&fb);
    if (!success) return;
    for (uint32_t i = CHAR_HEIGHT; i < fb.height; i++) {
        for (uint32_t j = 0; j < fb.width; j++) {
            uint32_t col;
            success = gui_framebuffer_get_pixel(j, i, &col);
            if (!success) return;
            success = gui_framebuffer_put_pixel_encoded(j, i-CHAR_HEIGHT, col);
            if (!success) return;
        }
    }

    // Clear last layer
    for (uint32_t i = 0; i < CHAR_HEIGHT + HEIGHT_BUFFER; i++) {
        for (uint32_t j = 0; j < fb.width; j++) {
            success = gui_framebuffer_put_pixel_encoded(j, fb.height - i - 1, BG_COLOR);
            if (!success) return;
        }
    }
}

void init_tty_gui(void) {
    gui_framebuffer_t fb;
    int success = gui_framebuffer_get(&fb);
    if (!success) {
        return;
    }

    MAX_COLS = fb.width / (CHAR_WIDTH + WIDTH_BUFFER);
    MAX_ROWS = fb.height / (CHAR_HEIGHT + HEIGHT_BUFFER);
    cursor_row = 0;
    cursor_col = 0;

    gui_framebuffer_clear(0x18, 0, 0x32);
}

void toggle_cursor(void) {
    for (uint32_t i = row_to_px(cursor_row); i < row_to_px(cursor_row) + 16; i++) {
        for (uint32_t j = col_to_px(cursor_col); j < col_to_px(cursor_col) + 8; j++) {
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

void cursor_advance_row(void) {
    toggle_cursor();
    if (cursor_row == MAX_ROWS-1) {
        scroll_down_row();
        cursor_col = 0;
    } else {
        cursor_col = 0;
        cursor_row++;
    }
    toggle_cursor();
}

void advance_cursor(void) {
    cursor_col++;
    if (cursor_col == MAX_COLS) {
        cursor_col = 0;
        cursor_row++;
        
        if (cursor_row == MAX_ROWS) {
            scroll_down_row();
            cursor_row--;
        }
    }
}

void tty_gui_cursor_left(void) {
    if (cursor_row == 0 && cursor_col == 0) {
        return;
    }

    toggle_cursor();
    if (cursor_col == 0) {
        cursor_row--;
        cursor_col = MAX_COLS - 1;
    } else {
        cursor_col--;
    }
    toggle_cursor();
}

void tty_gui_cursor_right(void) {
    if (cursor_row == MAX_ROWS - 1 && cursor_col == MAX_COLS - 1) {
        return;
    }

    toggle_cursor();
    cursor_col++;
    if (cursor_col == MAX_COLS) {
        cursor_col = 0;
        cursor_row++;
    }
    toggle_cursor();
}

void backtrack_cursor(void) {
    tty_gui_cursor_left();
}

static void clear_screen(void) {
    cursor_row = 0;
    cursor_col = 0;
    gui_framebuffer_clear(0x18, 0, 0x32);
    toggle_cursor();
}

static void write_tab(void) {
    int target_col = ((cursor_col / 8) + 1) * 8;
    do {
        tty_gui_write_char(' ');
    } while (cursor_col != 0 && cursor_col < target_col);
}

void tty_gui_write_char(const char c) {
    if (c == '\n') {
        cursor_advance_row();
    } else if (c == '\b') {
        backtrack_cursor();
    } else if (c == '\f') {
        clear_screen();
    } else if (c == '\t') {
        write_tab();
    } else {
        toggle_cursor();
        uint8_t *char_format = get_ascii_char_font(c);
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 16; j++) {
                gui_framebuffer_put_pixel_encoded(col_to_px(cursor_col)+i, row_to_px(cursor_row)+j, (char_format[j] & (1 << (7-i))) ? MAIN_COLOR : BG_COLOR);
            }
        }
        advance_cursor();
        toggle_cursor();
    }
}

int tty_gui_get_rows(void) {
    return MAX_ROWS;
}

int tty_gui_get_cols(void) {
    return MAX_COLS;
}

int tty_gui_get_cursor_row(void) {
    return cursor_row;
}

int tty_gui_get_cursor_col(void) {
    return cursor_col;
}
