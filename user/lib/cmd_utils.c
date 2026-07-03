#include "cmd_utils.h"

#include "fs_syscall.h"
#include "stdio.h"
#include "string.h"

#define CMD_BUF_SIZE 1024

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
    char buf[CMD_BUF_SIZE];

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

static int is_cat_output_arg(int argc, char **argv, int idx) {
    if (idx < 1 || idx >= argc || argv[idx] == NULL) {
        return 0;
    }
    if (argv[idx][0] != '-' || argv[idx][1] == '\0' || argv[idx][2] != '\0') {
        return 0;
    }
    return argv[idx][1] == 'a' || argv[idx][1] == 'w';
}

int cmd_cat(int argc, char **argv) {
    char *output_file = NULL;
    int output_flags = O_WRONLY | O_CREAT | O_TRUNC;
    int output_arg = -1;
    int output_path_arg = -1;

    for (int i = 1; i < argc; i++) {
        if (!is_cat_output_arg(argc, argv, i)) {
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

int cmd_cp(int argc, char **argv) {
    if (argc < 3) {
        printf("cp: usage: cp <src> <dest>\n");
        return -1;
    }

    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        return src_fd;
    }

    int dest_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (dest_fd < 0) {
        close(src_fd);
        return dest_fd;
    }

    int err = copy_fd(src_fd, dest_fd);
    int close_src_err = close(src_fd);
    int close_dest_err = close(dest_fd);
    if (err == 0 && close_src_err < 0) {
        err = close_src_err;
    }
    if (err == 0 && close_dest_err < 0) {
        err = close_dest_err;
    }

    return err;
}

int cmd_chmod(int argc, char **argv) {
    if (argc < 3) {
        printf("chmod: usage: chmod <mode> <file>\n");
        return -1;
    }

    int flag = 0;
    char *new_perms = argv[1];
    if (argv[1][0] == '-') {
        flag = 1;
        new_perms = argv[1] + 1;
    } else if (argv[1][0] == '+') {
        flag = 2;
        new_perms = argv[1] + 1;
    } else if (argv[1][0] == '=') {
        flag = 0;
        new_perms = argv[1] + 1;
    }

    return fs_chmod(argv[2], new_perms, flag);
}

int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        printf("touch: usage: touch <file>...\n");
        return -1;
    }

    return touch(argv + 1);
}

int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        printf("mkdir: usage: mkdir <dir>...\n");
        return -1;
    }

    return fs_mkdir(argv + 1);
}
