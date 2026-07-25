#pragma once

#include <stddef.h>
#include <stdint.h>

int usb_keyboard_char_driver_init(void);
int usb_keyboard_create_device_nodes(void);
void usb_keyboard_device_receive(const char *buffer, size_t count);
void usb_keyboard_get_counters(uint64_t *received, uint64_t *read);
