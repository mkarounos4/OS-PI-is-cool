#pragma once

#include <stdint.h>
#include "syscall.h"
#include "fs_syscall.h"
#include "signals.h"
#include "stdio.h"
#include "string.h"

void *shell_init(void *args);
