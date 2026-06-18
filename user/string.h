#pragma once

#include <stddef.h>

size_t strlen(const char *str);
int strcmp(const char *lhs, const char *rhs);
void *memcpy(void *dst, const void *src, size_t n);
long strtol(const char *nptr, char **endptr, int base);
