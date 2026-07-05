#include "lib/fs_syscall.h"
#include "lib/errno.h"
#include "lib/stdio.h"

#define CP_BUF_SIZE 1024

static int write_all(int fd, const char *buf, int n) {
    int written = 0;
    while (written < n) {
        int chunk = write(fd, buf + written, n - written);
        if (chunk <= 0) {
            return chunk < 0 ? chunk : -EIO;
        }
        written += chunk;
    }
    return written;
}

static int copy_fd(int in_fd, int out_fd) {
    char buf[CP_BUF_SIZE];

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

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("cp: usage: cp <src> <dest>\n");
        return -EINVAL;
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
