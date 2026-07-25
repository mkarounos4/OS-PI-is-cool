#include "hid_keyboard.h"

#include "string.h"
#include "usb_keyboard_device.h"

#define HID_MOD_LEFT_CTRL   (1u << 0)
#define HID_MOD_LEFT_SHIFT  (1u << 1)
#define HID_MOD_LEFT_ALT    (1u << 2)
#define HID_MOD_RIGHT_CTRL  (1u << 4)
#define HID_MOD_RIGHT_SHIFT (1u << 5)
#define HID_MOD_RIGHT_ALT   (1u << 6)

static const char unshifted[0x39] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0a] = 'g', [0x0b] = 'h',
    [0x0c] = 'i', [0x0d] = 'j', [0x0e] = 'k', [0x0f] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1a] = 'w', [0x1b] = 'x',
    [0x1c] = 'y', [0x1d] = 'z',
    [0x1e] = '1', [0x1f] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0',
    [0x28] = '\r', [0x29] = 0x1b, [0x2a] = 0x7f, [0x2b] = '\t',
    [0x2c] = ' ', [0x2d] = '-', [0x2e] = '=', [0x2f] = '[',
    [0x30] = ']', [0x31] = '\\', [0x33] = ';', [0x34] = '\'',
    [0x35] = '`', [0x36] = ',', [0x37] = '.', [0x38] = '/',
};

static const char shifted[0x39] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
    [0x08] = 'E', [0x09] = 'F', [0x0a] = 'G', [0x0b] = 'H',
    [0x0c] = 'I', [0x0d] = 'J', [0x0e] = 'K', [0x0f] = 'L',
    [0x10] = 'M', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
    [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1a] = 'W', [0x1b] = 'X',
    [0x1c] = 'Y', [0x1d] = 'Z',
    [0x1e] = '!', [0x1f] = '@', [0x20] = '#', [0x21] = '$',
    [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
    [0x26] = '(', [0x27] = ')',
    [0x28] = '\r', [0x29] = 0x1b, [0x2a] = 0x7f, [0x2b] = '\t',
    [0x2c] = ' ', [0x2d] = '_', [0x2e] = '+', [0x2f] = '{',
    [0x30] = '}', [0x31] = '|', [0x33] = ':', [0x34] = '"',
    [0x35] = '~', [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

static int report_contains(const uint8_t report[8], uint8_t usage) {
    for (unsigned i = 2; i < 8; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void send_sequence(const char *sequence, size_t length) {
    usb_keyboard_device_receive(sequence, length);
}

static void handle_usage(struct hid_keyboard *keyboard, uint8_t modifiers,
                         uint8_t usage) {
    int shift = modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT);
    int ctrl = modifiers & (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL);
    int alt = modifiers & (HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT);
    char ch = 0;

    if (usage == 0x39) {
        keyboard->caps_lock ^= 1;
        return;
    }

    if (usage >= 0x04 && usage <= 0x38) {
        int letter = usage >= 0x04 && usage <= 0x1d;
        ch = (shift ^ (letter && keyboard->caps_lock))
                 ? shifted[usage] : unshifted[usage];
        if (ctrl && letter) {
            ch = (char)(usage - 0x04 + 1);
        }
        if (ch != 0) {
            if (alt) {
                const char prefix = 0x1b;
                send_sequence(&prefix, 1);
            }
            send_sequence(&ch, 1);
        }
        return;
    }

    switch (usage) {
    case 0x4f: send_sequence("\x1b[C", 3); break; /* right */
    case 0x50: send_sequence("\x1b[D", 3); break; /* left */
    case 0x51: send_sequence("\x1b[B", 3); break; /* down */
    case 0x52: send_sequence("\x1b[A", 3); break; /* up */
    case 0x4a: send_sequence("\x1b[H", 3); break; /* home */
    case 0x4d: send_sequence("\x1b[F", 3); break; /* end */
    case 0x4c: send_sequence("\x1b[3~", 4); break; /* delete */
    default: break;
    }
}

void hid_keyboard_init(struct hid_keyboard *keyboard) {
    if (keyboard != NULL) {
        memset(keyboard, 0, sizeof(*keyboard));
    }
}

void hid_keyboard_process_report(struct hid_keyboard *keyboard,
                                 const uint8_t report[8]) {
    if (keyboard == NULL || report == NULL) {
        return;
    }

    /* Usage 1..3 means rollover/error; discard the entire report. */
    for (unsigned i = 2; i < 8; i++) {
        if (report[i] >= 1 && report[i] <= 3) {
            return;
        }
    }

    for (unsigned i = 2; i < 8; i++) {
        uint8_t usage = report[i];
        if (usage != 0 && !report_contains(keyboard->previous, usage)) {
            handle_usage(keyboard, report[0], usage);
        }
    }

    for (unsigned i = 0; i < 8; i++) {
        keyboard->previous[i] = report[i];
    }
}
