#pragma once

void init_tty_gui(void);
void tty_gui_write_char(const char c);
void tty_gui_write_char_for_tty(int terminal, const char c);
void tty_gui_activate_terminal(int terminal);
int tty_gui_get_active_terminal(void);
void tty_gui_set_terminal_visible(int terminal, int visible);
int tty_gui_is_terminal_visible(int terminal);
int tty_gui_next_visible_terminal(int from, int delta);
void tty_gui_cursor_left(void);
void tty_gui_cursor_right(void);
int tty_gui_get_rows(void);
int tty_gui_get_cols(void);
int tty_gui_get_cursor_row(void);
int tty_gui_get_cursor_col(void);
