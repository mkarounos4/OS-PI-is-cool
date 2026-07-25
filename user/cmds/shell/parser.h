#ifndef PARSER_H_
#define PARSER_H_

#include <stdbool.h>
#include "lib/string.h"
#include "lib/malloc.h"
#include "lib/fs_syscall.h"

#define UNEXPECTED_FILE_INPUT 1
#define UNEXPECTED_FILE_OUTPUT 2
#define UNEXPECTED_PIPELINE 3
#define UNEXPECTED_AMPERSAND 4
#define EXPECT_INPUT_FILENAME 5
#define EXPECT_OUTPUT_FILENAME 6
#define EXPECT_COMMANDS 7

struct parsed_command {
    bool is_background;

    bool is_file_append;

    bool is_here_document;

    const char *stdin_file;
    const char *stdout_file;

    size_t num_commands;
    char **commands[];
};

int parse_command(const char *cmd_line, struct parsed_command **result);

#endif
