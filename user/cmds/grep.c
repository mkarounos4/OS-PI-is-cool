
#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/stdio.h"
#include "lib/string.h"

#define GREP_BUF_SIZE 512
#define GREP_LINE_SIZE 1024

static int contains(const char *line, const char *pattern) {
    size_t pattern_len = strlen(pattern);
    if (pattern_len == 0) {
        return 1;
    }

    for (size_t i = 0; line[i] != '\0'; i++) {
        size_t j = 0;
        while (pattern[j] != '\0' && line[i + j] == pattern[j]) {
            j++;
        }
        if (j == pattern_len) {
            return 1;
        }
    }
    return 0;
}

static int write_all(int fd, const char *buf, int n) {
    int written = 0;
    while (written < n) {
        int chunk = write(fd, buf + written, n - written);
        if (chunk <= 0) {
            return chunk < 0 ? chunk : -EIO;
        }
        written += chunk;
    }
    return 0;
}

static int flush_line(const char *pattern, char *line, int line_len) {
    line[line_len] = '\0';
    if (!contains(line, pattern)) {
        return 0;
    }
    int err = write_all(STDOUT_FILENO, line, line_len);
    if (err < 0) {
        return err;
    }
    if (line_len == 0 || line[line_len - 1] != '\n') {
        return write_all(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int grep_fd(int fd, const char *pattern) {
    char buf[GREP_BUF_SIZE];
    char line[GREP_LINE_SIZE + 1];
    int line_len = 0;

    while (1) {
        int bytes = read(fd, buf, sizeof(buf));
        if (bytes < 0) {
            return bytes;
        }
        if (bytes == 0) {
            break;
        }

        for (int i = 0; i < bytes; i++) {
            if (line_len < GREP_LINE_SIZE) {
                line[line_len++] = buf[i];
            }
            if (buf[i] == '\n') {
                int err = flush_line(pattern, line, line_len);
                if (err < 0) {
                    return err;
                }
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        return flush_line(pattern, line, line_len);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_errno("grep", "usage: grep <pattern> [file...]", -EINVAL);
        return -EINVAL;
    }

    if (argc == 2) {
        int err = grep_fd(STDIN_FILENO, argv[1]);
        if (err < 0) {
            print_errno("grep", "stdin", err);
        }
        return err;
    }

    int ret = 0;
    for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            print_errno("grep", argv[i], fd);
            ret = fd;
            continue;
        }
        int err = grep_fd(fd, argv[1]);
        int close_err = close(fd);
        if (err < 0) {
            print_errno("grep", argv[i], err);
            ret = err;
        } else if (close_err < 0) {
            print_errno("grep", argv[i], close_err);
            ret = close_err;
        }
    }
    return ret;
}
