#pragma once

#include "malloc.h"

size_t strlen(const char *str);
int strcmp(const char *lhs, const char *rhs);
long strtol(const char *nptr, char **endptr, int base);
char *str_concat(char *str1, char *str2);
char *str_copy(char *str);
int isspace(int c);
