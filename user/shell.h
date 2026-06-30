#pragma once

#include <stdint.h>
#include "syscall.h"
#include "fs_syscall.h"
#include "signals.h"
#include "stdio.h"
#include "string.h"
#include "shell/Vec.h"
#include "shell/io-helpers.h"
#include "shell/Job.h"
#include "shell/parser.h"
#include "malloc.h"
#include "shell_cmds.h"

#define EXIT_FAILURE -1
#define EXIT_SUCCESS 0

#define READ_END 0
#define WRITE_END 1
#define O_CLOEXEC 1

#define BUF_SIZE 40

void *shell_init(void *args);

static inline void perror(const char *msg) {
    write(2, msg, strlen(msg));
}
