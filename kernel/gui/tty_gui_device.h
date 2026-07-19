#pragma once

int tty_gui_char_driver_init(void);
int tty_gui_create_device_nodes(void);
int tty_gui_char_device_activate(int minor);
void tty_gui_char_device_deactivate(int minor);
