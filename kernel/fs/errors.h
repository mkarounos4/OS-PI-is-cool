#pragma once
#include "uart/uart.h"

#define SUCCESS 0
#define FILE_WRITE_ERROR -1
#define FILE_OPEN_ERROR -2
#define FILE_READ_ERROR -3
#define FILE_SEEK_ERROR -4
#define FS_ALREADY_MOUNTED -5
#define FS_NOT_MOUNTED -6
#define NO_FREE_BLOCKS -7
#define INODE_FULL -8
#define INVALID_ARGS -9
#define F_ONLY_ONE_WRITER -10
#define OFT_FD_DOES_NOT_EXIST -11
#define FAT_NO_SPACE_REMAINING -12
#define FILE_NOT_FOUND -13
#define ILLEGAL_BLOCK_NO -14
#define END_DIR_NOT_FOUND -15
#define INVALID_PERMISSIONS -16
#define INVALID_FILE_NAME -17
#define PID_NOT_FOUND -18
#define FILE_NOT_CREATED -19
#define CAT_SAME_INPUT_OUTPUT -20
#define FS_INVALID -21

typedef int err_t;

void print_error(err_t error);
long fs_err_to_sys_errno(long error);
