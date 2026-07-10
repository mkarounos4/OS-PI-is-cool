#pragma once

#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EBUSY    16
#define EEXIST   17
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOSPC   28
#define ESPIPE   29
#define EPIPE    32
#define ERANGE   34
#define ENAMETOOLONG 36
#define ENOSYS   38

const char *errno_name(long err);
const char *errno_message(long err);
void print_errno(const char *cmd, const char *context, long err);
