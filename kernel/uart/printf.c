#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "kapi.h"
#include "oft.h"


struct format_out {
    char *buf;
    size_t size;
    size_t len;
};

static void format_emit(struct format_out *out, char c) {
    if (out->size != 0 && out->len + 1 < out->size) {
        out->buf[out->len] = c;
    }
    out->len++;
}

static void format_str(struct format_out *out, const char *s) {
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0') {
        format_emit(out, *s++);
    }
}

static void format_uint(struct format_out *out, uint64_t value, unsigned base) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        format_emit(out, '0');
        return;
    }

    while (value != 0 && i < (int)sizeof(buf)) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    while (i > 0) {
        format_emit(out, buf[--i]);
    }
}

static void format_int(struct format_out *out, int64_t value) {
    if (value < 0) {
        format_emit(out, '-');
        format_uint(out, (uint64_t)(-value), 10);
        return;
    }

    format_uint(out, (uint64_t)value, 10);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    struct format_out out = {
        .buf = buf,
        .size = size,
        .len = 0,
    };

    while (*fmt != '\0') {
        if (*fmt != '%') {
            format_emit(&out, *fmt++);
            continue;
        }

        fmt++;
        int long_arg = 0;
        if (*fmt == 'l') {
            long_arg = 1;
            fmt++;
            if (*fmt == 'l') {
                fmt++;
            }
        }

        switch (*fmt) {
        case 'c':
            format_emit(&out, (char)va_arg(args, int));
            break;
        case 'd':
            format_int(&out, long_arg ? va_arg(args, long) : va_arg(args, int));
            break;
        case 's':
            format_str(&out, va_arg(args, const char *));
            break;
        case 'u':
            format_uint(&out, long_arg ? va_arg(args, unsigned long) :
                        va_arg(args, unsigned int), 10);
            break;
        case 'x':
        case 'X':
            format_uint(&out, long_arg ? va_arg(args, unsigned long) :
                        va_arg(args, unsigned long), 16);
            break;
        case '%':
            format_emit(&out, '%');
            break;
        default:
            format_emit(&out, '%');
            format_emit(&out, *fmt);
            break;
        }

        fmt++;
    }

    if (size != 0) {
        size_t nul_pos = out.len < size ? out.len : size - 1;
        buf[nul_pos] = '\0';
    }

    return (int)out.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

static void uart_write_formatted(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
}

/* 
 * printf() implementation with support for the following
 * %c, %d, %s, %u, %x, and %X
*/
void printf(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len >= (int)sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }

    uart_write_formatted(buf, len);
}

int fprintf(int fd, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len >= (int)sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }

    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err) {
        return err;
    }
    int written = k_write(entry, buf, len);
    if (written < 0) {
        return written;
    }
    return len;
}
