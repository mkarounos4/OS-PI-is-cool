#include "errors.h"

void print_error(err_t error) {
    switch(error) {
        case FILE_WRITE_ERROR:
            printf("Error: write to file failed.\n");
            break;
        case FILE_OPEN_ERROR:
            printf("Error: failed to open file\n");
            break;
        case FILE_READ_ERROR:
            printf("Error: failed to read file.\n");
            break;
        case FILE_SEEK_ERROR:
            printf("Error: Failed to seek file.\n");
            break;
        case FS_ALREADY_MOUNTED:
            printf("Error: file system is already mounted.\n");
            break;
        case FS_NOT_MOUNTED:
            printf("Error: File system is not mounted.\n");
            break;
        case INODE_FULL:
        case NO_FREE_BLOCKS:
        case FAT_NO_SPACE_REMAINING:
            printf("Error: File System out of free blocks.\n");
            break;
        case INVALID_ARGS:
            printf("Error: Invalid arguments to commands.\n");
            break;
        case F_ONLY_ONE_WRITER:
            printf("Error: File already has a writer.\n");
            break;
        case OFT_FD_DOES_NOT_EXIST:
        case FILE_NOT_FOUND:
            printf("Error: file does not exist.\n");
            break;
        case ILLEGAL_BLOCK_NO:
            printf("Error: Tried to access illegal block number.\n");
            break;
        case END_DIR_NOT_FOUND:
            printf("Error: Could not find end directory for file\n");
            break;
        case INVALID_PERMISSIONS:
            printf("Error: invalid permissions for accessing file.\n");
            break;
        case INVALID_FILE_NAME:
            printf("Error: File name contains invalid characters.\n");
            break;
        case PID_NOT_FOUND:
            printf("Error: Process not found\n");
            break;
        case CAT_SAME_INPUT_OUTPUT:
            printf("Error: cat input and output file cannot be the same.\n");
            break;
        case FS_INVALID:
            printf("Error: invalid or missing file system.\n");
            break;
    }
}
