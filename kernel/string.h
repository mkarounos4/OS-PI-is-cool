#pragma once

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *ptr, int value, size_t n);

size_t strlen(const char *str);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *lhs, const char *rhs);
char *strtok(char *str, const char *delim);
int atoi(const char *str, int *out);
