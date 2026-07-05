#pragma once

#include <stdint.h>

#include "scheduler/process.h"
#include "traps/traps.h"

int elf_exec_process(pcb_t *pcb, const char *path, char *const argv[],
                     struct trap_frame *frame, uint64_t frame_va,
                     struct trap_frame **next_frame, int install_table);

