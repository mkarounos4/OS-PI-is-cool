#include "lib/fs_syscall.h"
#include "lib/stdio.h"
#include "lib/string.h"

#define CAT_BUF_SIZE 1024

static int write_all(int fd, const char *buf, int n) {
    int written = 0;
    while (written < n) {
        int chunk = write(fd, buf + written, n - written);
        if (chunk <= 0) {
            return chunk < 0 ? chunk : -1;
        }
        written += chunk;
    }
    return written;
}

static int copy_fd(int in_fd, int out_fd) {
    char buf[CAT_BUF_SIZE];

    while (1) {
        int bytes_read = read(in_fd, buf, sizeof(buf));
        if (bytes_read < 0) {
            return bytes_read;
        }
        if (bytes_read == 0) {
            return 0;
        }

        int bytes_written = write_all(out_fd, buf, bytes_read);
        if (bytes_written < 0) {
            return bytes_written;
        }
    }
}

static int is_output_arg(int argc, char **argv, int idx) {
    if (idx < 1 || idx >= argc || argv[idx] == NULL) {
        return 0;
    }
    if (argv[idx][0] != '-' || argv[idx][1] == '\0' || argv[idx][2] != '\0') {
        return 0;
    }
    return argv[idx][1] == 'a' || argv[idx][1] == 'w';
}

int main(int argc, char **argv) {
    char *output_file = NULL;
    int output_flags = O_WRONLY | O_CREAT | O_TRUNC;
    int output_arg = -1;
    int output_path_arg = -1;

    for (int i = 1; i < argc; i++) {
        if (!is_output_arg(argc, argv, i)) {
            continue;
        }
        if (i + 1 >= argc || argv[i + 1] == NULL) {
            printf("cat: missing output file\n");
            return -1;
        }

        output_file = argv[i + 1];
        output_flags = O_WRONLY | O_CREAT;
        output_flags |= argv[i][1] == 'a' ? O_APPEND : O_TRUNC;
        output_arg = i;
        output_path_arg = i + 1;
        break;
    }

    int out_fd = STDOUT_FILENO;
    if (output_file != NULL) {
        out_fd = open(output_file, output_flags);
        if (out_fd < 0) {
            return out_fd;
        }
    }

    int saw_input = 0;
    int err = 0;
    for (int i = 1; i < argc; i++) {
        if (i == output_arg || i == output_path_arg) {
            continue;
        }
        if (output_file != NULL && strcmp(argv[i], output_file) == 0) {
            err = -1;
            break;
        }

        saw_input = 1;
        int in_fd = open(argv[i], O_RDONLY);
        if (in_fd < 0) {
            err = in_fd;
            break;
        }

        err = copy_fd(in_fd, out_fd);
        int close_err = close(in_fd);
        if (err == 0 && close_err < 0) {
            err = close_err;
        }
        if (err < 0) {
            break;
        }
    }

    if (!saw_input && err == 0) {
        err = copy_fd(STDIN_FILENO, out_fd);
    }

    if (output_file != NULL) {
        int close_err = close(out_fd);
        if (err == 0 && close_err < 0) {
            err = close_err;
        }
    }

    return err;
}
