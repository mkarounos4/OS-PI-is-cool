#pragma once

/*
 * Start the Raspberry Pi 5 RP1 USB host controllers and the HID keyboard
 * polling service.  Returns the number of controllers successfully started.
 */
int xhci_keyboard_init(void);
int xhci_keyboard_format_status(char *buffer, unsigned long size);
