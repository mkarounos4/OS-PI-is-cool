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

// Concatenates 2 strings. Result is malloc'd.
// Note: does not free str1/str2.
char *str_concat(char *str1, char *str2) {
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char *new_str = malloc(sizeof(char)*(len1+len2+1));
    if (new_str == NULL) {
        printf("Malloc failed on str_concat\n");
        return NULL;
    }
    for (int i = 0; i < len1; i++) {
        new_str[i] = str1[i];
    }
    for (int i = 0; i < len2; i++) {
        new_str[len1+i] = str2[i];
    }
    new_str[len1+len2] = '\0';
    return new_str;
}

// Copies str into a new string. Result is malloc'd.
char *str_copy(char *str) {
    size_t len = strlen(str);
    char *new_str = malloc(sizeof(char)*(len+1));
    for (int i = 0; i <= len; i++) {
        new_str[i] = str[i];
    }
    return new_str;
}

int isspace(int c) {
    return (c == ' '  ||  // Space (0x20)
            c == '\t' ||  // Horizontal Tab (0x09)
            c == '\n' ||  // Newline / Line Feed (0x0a)
            c == '\v' ||  // Vertical Tab (0x0b)
            c == '\f' ||  // Form Feed (0x0c)
            c == '\r');   // Carriage Return (0x0d)
}
