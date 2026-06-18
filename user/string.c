#include "string.h"

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }
    return (unsigned char)*lhs - (unsigned char)*rhs;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    long sign = 1;
    long value = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') {
        s++;
    }

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (base == 0) {
        base = 10;
    }

    const char *digits_start = s;
    while (*s != '\0') {
        int digit;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        value = value * base + digit;
        s++;
    }

    if (endptr != NULL) {
        *endptr = (char *)(s == digits_start ? nptr : s);
    }

    return value * sign;
}
