#include "../../headers/shell-helpers/str-helpers.h"

// Concatenates 2 strings. Result is malloc'd.
// Note: does not free str1/str2.
char *str_concat(char *str1, char *str2) {
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char *new_str = malloc(sizeof(char)*(len1+len2+1));
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
