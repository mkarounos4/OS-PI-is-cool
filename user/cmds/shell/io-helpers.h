#pragma once

#include "parser.h"
#include "lib/fs_syscall.h"
#include "lib/malloc.h"

// Redirect std in to input file specified in cmd
int changeStdInput(struct parsed_command* cmd);

// Redirect std out to output file specified in cmd
int changeStdOutput(struct parsed_command* cmd);
