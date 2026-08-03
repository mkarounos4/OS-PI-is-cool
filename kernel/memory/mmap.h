#pragma once
#include "memory.h"
#include <stdint.h>

#define MMAP_PROT_NONE 0
#define MMAP_PROT_EXEC 1
#define MMAP_PROT_WRITE 2
#define MMAP_PROT_READ 4

#define MAP_PRIVATE 1
#define MAP_SHARED 2
#define MAP_ANONYMOUS 4
#define MAP_FIXED 8

#define INVALID_INO 0

void *mmap(void *addr, size_t length, int prot, int flags, int fd, uint32_t offset);
int munmap(void *addr, size_t length);
int msync(void *addr, size_t length, int flags);
int mprotect(void *addr, size_t length, int prot);
int madvise(void *addr, size_t length, int advice);
