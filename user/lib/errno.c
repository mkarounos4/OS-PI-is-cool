#include "errno.h"

#include "fs_syscall.h"
#include "string.h"

static long errno_code(long err) {
    return err < 0 ? -err : err;
}

const char *errno_name(long err) {
    switch (errno_code(err)) {
    case EPERM: return "EPERM";
    case ENOENT: return "ENOENT";
    case ESRCH: return "ESRCH";
    case EINTR: return "EINTR";
    case EIO: return "EIO";
    case EBADF: return "EBADF";
    case ECHILD: return "ECHILD";
    case EAGAIN: return "EAGAIN";
    case ENOMEM: return "ENOMEM";
    case EACCES: return "EACCES";
    case EFAULT: return "EFAULT";
    case EBUSY: return "EBUSY";
    case EEXIST: return "EEXIST";
    case ENODEV: return "ENODEV";
    case ENOTDIR: return "ENOTDIR";
    case EISDIR: return "EISDIR";
    case EINVAL: return "EINVAL";
    case ENFILE: return "ENFILE";
    case EMFILE: return "EMFILE";
    case ENOSPC: return "ENOSPC";
    case ESPIPE: return "ESPIPE";
    case EPIPE: return "EPIPE";
    case ERANGE: return "ERANGE";
    case ENAMETOOLONG: return "ENAMETOOLONG";
    case ENOSYS: return "ENOSYS";
    default: return "EUNKNOWN";
    }
}

const char *errno_message(long err) {
    switch (errno_code(err)) {
    case EPERM: return "operation not permitted";
    case ENOENT: return "no such file or directory";
    case ESRCH: return "no such process";
    case EINTR: return "interrupted system call";
    case EIO: return "I/O error";
    case EBADF: return "bad file descriptor";
    case ECHILD: return "no child processes";
    case EAGAIN: return "try again";
    case ENOMEM: return "out of memory";
    case EACCES: return "permission denied";
    case EFAULT: return "bad address";
    case EBUSY: return "resource busy";
    case EEXIST: return "file exists";
    case ENODEV: return "no such device";
    case ENOTDIR: return "not a directory";
    case EISDIR: return "is a directory";
    case EINVAL: return "invalid argument";
    case ENFILE: return "file table overflow";
    case EMFILE: return "too many open files";
    case ENOSPC: return "no space left on device";
    case ESPIPE: return "illegal seek";
    case EPIPE: return "broken pipe";
    case ERANGE: return "result out of range";
    case ENAMETOOLONG: return "file name too long";
    case ENOSYS: return "function not implemented";
    default: return "unknown error";
    }
}

static void write_str(int fd, const char *s) {
    write(fd, s, (int)strlen(s));
}

static void write_long(int fd, long value) {
    char buf[32];
    int i = 0;

    if (value < 0) {
        write_str(fd, "-");
        value = -value;
    }

    if (value == 0) {
        write_str(fd, "0");
        return;
    }

    while (value != 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        char c = buf[--i];
        write(fd, &c, 1);
    }
}

void print_errno(const char *cmd, const char *context, long err) {
    if (err >= 0) {
        return;
    }

    if (cmd != 0) {
        write_str(STDERR_FILENO, cmd);
        write_str(STDERR_FILENO, ": ");
    }
    if (context != 0) {
        write_str(STDERR_FILENO, context);
        write_str(STDERR_FILENO, ": ");
    }

    write_str(STDERR_FILENO, errno_name(err));
    write_str(STDERR_FILENO, " (");
    write_long(STDERR_FILENO, err);
    write_str(STDERR_FILENO, "): ");
    write_str(STDERR_FILENO, errno_message(err));
    write_str(STDERR_FILENO, "\n");
}
