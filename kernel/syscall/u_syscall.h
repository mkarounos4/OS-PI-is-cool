
#pragma once

#include <stdint.h>

#include "scheduler/process.h"

long write_console(const char *s, uint64_t len);
long putc(char c);
long get_ticks(void);
long delay(uint64_t ms);
long exit(int code);
long getpid(void);
long spawn(void *(*func)(void *), void *arg);
long waitpid(pid_t pid, int *status, uint32_t flags);
long sbrk(int64_t increment);
long kill(pid_t pid, int signal);
long block_until_event(uint32_t events);