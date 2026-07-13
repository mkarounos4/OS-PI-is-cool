#pragma once

void init_tty_gui(void);
void tty_gui_write_char(const char c);
int tty_gui_get_rows(void);
int tty_gui_get_cols(void);
int tty_gui_get_cursor_row(void);
int tty_gui_get_cursor_col(void);
