#include "parser.h"

static inline void skip_word(const char **const cur, const char *const end) {
    while (*cur < end && **cur != '<' && **cur != '>' &&
           **cur != '|' && **cur != '&' && !isspace(**cur)) {
        ++*cur;
    }
}

static inline void skip_space(const char **const cur, const char *const end) {
    while (*cur < end && isspace(**cur)) {
        ++*cur;
    }
}

int parse_command(const char *const cmd_line, struct parsed_command **const result) {
#define PARSE_FAIL(code) do { ret_code = (code); goto PROCESS_ERROR; } while (0)

    int ret_code = -1;

    const char *start = cmd_line;
    const char *end = cmd_line + strlen(cmd_line);

    for (const char *cur = start; cur < end; ++cur)
        if (*cur == '#') {
            end = cur;
            break;
        }

    while (start < end && isspace(*start)) ++start;
    while (start < end && isspace(end[-1])) --end;

    struct parsed_command *pcmd = calloc(1, sizeof(struct parsed_command));
    if (pcmd == NULL) return -1;
    if (start == end) goto PROCESS_SUCCESS;

    if (end[-1] == '&') {
        pcmd->is_background = true;
        --end;
    }

    int total_strings = 0;
    {
        bool has_token_last = false, has_file_input = false, has_file_output = false;
        const char *skipped;
        for (const char *cur = start; cur < end; skip_space(&cur, end))
            switch (cur[0]) {
                case '&':
                    PARSE_FAIL(UNEXPECTED_AMPERSAND);
                case '<':
                    if (pcmd->num_commands > 0 || has_file_input) PARSE_FAIL(UNEXPECTED_FILE_INPUT);

                    ++cur;
                    if (cur < end && cur[0] == '<') {
                        pcmd->is_here_document = true;
                        ++cur;
                    }
                    skip_space(&cur, end);

                    skipped = cur;
                    skip_word(&skipped, end);
                    if (skipped <= cur) PARSE_FAIL(EXPECT_INPUT_FILENAME);

                    cur = skipped;
                    has_file_input = true;
                    break;
                case '>':
                    if (has_file_output) PARSE_FAIL(UNEXPECTED_FILE_OUTPUT);
                    if (cur + 1 < end && cur[1] == '>') {
                        pcmd->is_file_append = true;
                        ++cur;
                    }

                    ++cur;
                    skip_space(&cur, end);

                    skipped = cur;
                    skip_word(&skipped, end);
                    if (skipped <= cur) PARSE_FAIL(EXPECT_OUTPUT_FILENAME);

                    cur = skipped;
                    has_file_output = true;
                    break;
                case '|':
                    if (has_file_output) PARSE_FAIL(UNEXPECTED_FILE_OUTPUT);
                    if (!has_token_last) PARSE_FAIL(UNEXPECTED_PIPELINE);
                    has_token_last = false;
                    ++pcmd->num_commands;
                    ++cur;
                    break;
                default:
                    has_token_last = true;
                    ++total_strings;
                    skip_word(&cur, end);
            }

        if (total_strings == 0) {
            if (pcmd->is_background || has_file_input || has_file_output)
                PARSE_FAIL(EXPECT_COMMANDS);
            goto PROCESS_SUCCESS;
        }

        if (!has_token_last) PARSE_FAIL(UNEXPECTED_PIPELINE);
    }
    ++pcmd->num_commands;

    const size_t command_array_bytes =
        pcmd->num_commands * sizeof(char **);
    const size_t argv_array_bytes =
        (pcmd->num_commands + total_strings) * sizeof(char *);
    const size_t start_of_array =
        offsetof(struct parsed_command, commands) + command_array_bytes;
    const size_t start_of_str = start_of_array + argv_array_bytes;
    const size_t slen = end - start;

    char *const new_buf = realloc(pcmd, start_of_str + slen + 1);
    if (new_buf == NULL) goto PROCESS_ERROR;
    pcmd = (struct parsed_command *) new_buf;

    char *const new_start = memcpy(new_buf + start_of_str, start, slen);

    size_t cur_cmd = 0;
    char **argv_ptr = (char **) (new_buf + start_of_array);

    pcmd->commands[cur_cmd] = argv_ptr;
    for (const char *cur = start; cur < end; skip_space(&cur, end)) {
        switch (cur[0]) {
            case '<':
                ++cur;
                if (pcmd->is_here_document) ++cur;
                skip_space(&cur, end);
                pcmd->stdin_file = new_start + (cur - start);
                skip_word(&cur, end);
                new_start[cur - start] = '\0';
                break;
            case '>':
                if (pcmd->is_file_append) ++cur;
                ++cur;
                skip_space(&cur, end);
                pcmd->stdout_file = new_start + (cur - start);
                skip_word(&cur, end);
                new_start[cur - start] = '\0';
                break;
            case '|':
                *(argv_ptr++) = NULL;
                pcmd->commands[++cur_cmd] = argv_ptr;
                ++cur;
                break;
            default:
                *(argv_ptr++) = new_start + (cur - start);
                skip_word(&cur, end);
                new_start[cur - start] = '\0';
        }
    }
    *argv_ptr = NULL;

PROCESS_SUCCESS:
    *result = pcmd;
    return 0;
PROCESS_ERROR:
    free(pcmd);
    return ret_code;
#undef PARSE_FAIL
}
