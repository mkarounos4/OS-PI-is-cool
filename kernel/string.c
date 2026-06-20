#include "string.h"
#include "memory/malloc.h"

size_t strlen(const char *str) {
    size_t len = 0;

    while (str[len] != '\0') {
        len++;
    }

    return len;
}

char *strcpy(char *dst, const char *src) {
    size_t i = 0;

    do {
        dst[i] = src[i];
    } while (src[i++] != '\0');

    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;

    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    while (i < n) {
        dst[i] = '\0';
        i++;
    }

    return dst;
}

int strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }

    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

static int is_delim(char ch, const char *delim) {
    while (*delim != '\0') {
        if (ch == *delim) {
            return 1;
        }
        delim++;
    }

    return 0;
}

char *strtok(char *str, const char *delim) {
    static char *next;

    if (str != NULL) {
        next = str;
    }

    if (next == NULL) {
        return NULL;
    }

    while (*next != '\0' && is_delim(*next, delim)) {
        next++;
    }

    if (*next == '\0') {
        next = NULL;
        return NULL;
    }

    char *token = next;
    while (*next != '\0' && !is_delim(*next, delim)) {
        next++;
    }

    if (*next == '\0') {
        next = NULL;
    } else {
        *next = '\0';
        next++;
    }

    return token;
}

int atoi(const char *str, int *out) {
    if (str == NULL || out == NULL) {
        return -1;
    }

    while (*str == ' ' || *str == '\t' || *str == '\n' ||
           *str == '\r' || *str == '\v' || *str == '\f') {
        str++;
    }

    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    if (*str < '0' || *str > '9') {
        return -1;
    }

    int value = 0;
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }

    if (*str != '\0') {
        return -1;
    }

    *out = value * sign;
    return 0;
}
