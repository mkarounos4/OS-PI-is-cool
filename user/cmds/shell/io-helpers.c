#include "io-helpers.h"

#define RDWR_MODE 0644

int changeStdInput(struct parsed_command* cmd) {
    // Redirect stdin to input file if specified
    int input_file = open(cmd->stdin_file, O_RDONLY | RDWR_MODE);
    if (input_file == -1) {
        free(cmd);
        return 1;
    }
    
    if (dup2(input_file, STDIN_FILENO) == -1) {
        close(input_file);
        free(cmd);
        return 2;
    }
    
    close(input_file);
    return 0;
} 

int changeStdOutput(struct parsed_command* cmd) {
    int flags = O_WRONLY | O_CREAT;
    if (cmd->is_file_append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }
    
    int output_file = open(cmd->stdout_file, flags); //TODO: open flag RDWR_MODE
    if (output_file == -1) {
        free(cmd);
        return 1;
    }
    
    if (dup2(output_file, STDOUT_FILENO) == -1) {
        close(output_file);
        free(cmd);
        return 2;
    }
    
    close(output_file);
    return 0;
}
