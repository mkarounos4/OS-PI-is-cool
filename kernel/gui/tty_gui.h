#pragma once

void init_tty_gui(void);
void tty_gui_write_char(const char c);
void tty_gui_write_char_for_tty(int terminal, const char c);
void tty_gui_activate_terminal(int terminal);
int tty_gui_get_active_terminal(void);
void tty_gui_cursor_left(void);
void tty_gui_cursor_right(void);
int tty_gui_get_rows(void);
int tty_gui_get_cols(void);
int tty_gui_get_cursor_row(void);
int tty_gui_get_cursor_col(void);
