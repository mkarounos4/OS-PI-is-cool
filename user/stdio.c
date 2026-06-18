#include "stdio.h"

#include <stdarg.h>
#include <stdint.h>

#include "fs_syscall.h"
#include "string.h"

static int print_char(char c) {
    return write(1, &c, 1);
}

static int print_str(const char *s) {
    if (s == 0) {
        s = "(null)";
    }
    return write(1, (char *)s, (int)strlen(s));
}

static int print_uint(uint64_t value, unsigned base) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        return print_char('0');
    }

    while (value != 0 && i < (int)sizeof(buf)) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    int written = 0;
    while (i > 0) {
        written += print_char(buf[--i]);
    }
    return written;
}

static int print_int(int value) {
    if (value < 0) {
        print_char('-');
        return 1 + print_uint((uint64_t)(-value), 10);
    }
    return print_uint((uint64_t)value, 10);
}

int printf(const char *fmt, ...) {
    va_list args;
    int written = 0;

    va_start(args, fmt);
    while (*fmt != '\0') {
        if (*fmt != '%') {
            written += print_char(*fmt++);
            continue;
        }

        fmt++;
        if (*fmt == 's') {
            written += print_str(va_arg(args, const char *));
        } else if (*fmt == 'd') {
            written += print_int(va_arg(args, int));
        } else if (*fmt == 'u') {
            written += print_uint(va_arg(args, unsigned int), 10);
        } else if (*fmt == 'x') {
            written += print_uint(va_arg(args, unsigned int), 16);
        } else if (*fmt == '%') {
            written += print_char('%');
        } else {
            written += print_char('%');
            written += print_char(*fmt);
        }
        fmt++;
    }
    va_end(args);

    return written;
}

int puts(const char *s) {
    int written = print_str(s);
    written += print_char('\n');
    return written;
}
