#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct user_bin_st {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
} user_bin_t;

extern const user_bin_t user_bins[];
extern const size_t user_bins_count;
