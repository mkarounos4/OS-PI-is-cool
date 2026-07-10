
#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/stdio.h"
#include "lib/string.h"

#define WC_BUF_SIZE 512

typedef struct wc_counts_st {
    unsigned int lines;
    unsigned int words;
    unsigned int bytes;
} wc_counts_t;

static int wc_fd(int fd, wc_counts_t *counts) {
    char buf[WC_BUF_SIZE];
    int in_word = 0;

    while (1) {
        int n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            return n;
        }
        if (n == 0) {
            return 0;
        }

        counts->bytes += (unsigned int)n;
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                counts->lines++;
            }
            if (isspace((unsigned char)buf[i])) {
                in_word = 0;
            } else if (!in_word) {
                counts->words++;
                in_word = 1;
            }
        }
    }
}

static void print_counts(const wc_counts_t *counts, const char *name) {
    printf("%u %u %u", counts->lines, counts->words, counts->bytes);
    if (name != NULL) {
        printf(" %s", name);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc == 1) {
        wc_counts_t counts = {0, 0, 0};
        int err = wc_fd(STDIN_FILENO, &counts);
        if (err < 0) {
            print_errno("wc", "stdin", err);
            return err;
        }
        print_counts(&counts, NULL);
        return 0;
    }

    int ret = 0;
    wc_counts_t total = {0, 0, 0};
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            print_errno("wc", argv[i], fd);
            ret = fd;
            continue;
        }

        wc_counts_t counts = {0, 0, 0};
        int err = wc_fd(fd, &counts);
        int close_err = close(fd);
        if (err < 0) {
            print_errno("wc", argv[i], err);
            ret = err;
            continue;
        }
        if (close_err < 0) {
            print_errno("wc", argv[i], close_err);
            ret = close_err;
            continue;
        }

        print_counts(&counts, argv[i]);
        total.lines += counts.lines;
        total.words += counts.words;
        total.bytes += counts.bytes;
    }

    if (argc > 2) {
        print_counts(&total, "total");
    }
    return ret;
}
