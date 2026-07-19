#pragma once

#include <stddef.h>

int uart_char_driver_init(void);
int uart_create_device_nodes(void);
void uart_char_device_receive(const char *buffer, size_t count);

