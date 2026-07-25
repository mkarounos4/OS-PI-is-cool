#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Translate an 8-byte USB HID boot-keyboard report and deliver newly pressed
 * keys to the active TTY.  State is per physical keyboard.
 */
struct hid_keyboard {
    uint8_t previous[8];
    uint8_t caps_lock;
};

void hid_keyboard_init(struct hid_keyboard *keyboard);
void hid_keyboard_process_report(struct hid_keyboard *keyboard,
                                 const uint8_t report[8]);

