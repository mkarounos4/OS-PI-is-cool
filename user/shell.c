#include "shell.h"

volatile int wasInterrupted = 0;
int shell_pgid;
Vec background_status_updates;
jid_t curr_job_id = 0;
Vec background_jobs;
Vec stopped_background_jobs;

void ctrCHandler(int signum) {
    (void)signum;
    printf("\n");
    wasInterrupted = 1;
}

void ctrZHandler(int signum) { (void)signum; }

void prompt();
void wait_on_children();
void prepare_child_process(struct parsed_command *parsed_cmd);
void start_fg_job(job *newJob);
void execute_commands(struct parsed_command *parsed_cmd, char *cmd);
int handle_job_builtins(struct parsed_command *parsed_cmd);
char *get_input(int *nextAddNewLine);
void print_status_updates();
int execvp(const char *cmd, char **args);

void *shell_init(void *args) {
    setpgid(0, 0);

    int shell_num = (int)(uintptr_t)args;
    char path[10];
    path[0] = '/';
    path[1] = 'd';
    path[2] = 'e';
    path[3] = 'v';
    path[4] = '/';
    path[5] = 't';
    path[6] = 't';
    path[7] = 'y';
    path[8] = '0' + shell_num;
    path[9] = '\0';

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
        putstr("2\n");

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        return NULL;
    }

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        return NULL;
    }

    // Setup ctr-C handling
    struct sigaction sig_a;
    sigemptyset(&sig_a.sa_mask);
    sig_a.sa_handler = ctrCHandler;
    sig_a.sa_flags = 0;
    sigaction(SIGINT, &sig_a, NULL);

    struct sigaction sig_a_ctr_z;
    sigemptyset(&sig_a_ctr_z.sa_mask);
    sig_a_ctr_z.sa_handler = ctrZHandler;
    sig_a_ctr_z.sa_flags = 0; // SA_RESTART here
    sigaction(SIGTSTP, &sig_a_ctr_z, NULL);

	// Setup SIGTSP + SIGTTOU blocking
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGTTOU);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    // give shell tty control
    tcsetpgrp(shell_num, getpgrp());

	// Setup vectors
    background_status_updates = vec_new(1, free_dtor);
    background_jobs = vec_new(2, free_job);
    stopped_background_jobs = vec_new(2, NULL);

    prompt();

    return 0;
}


void exit_shell() {
}

int get_fg_job_pid(void) {
    return -1;
}

int is_integer(const char *str) {
    char *endptr;
    strtol(str, &endptr, 10);
    return (str != endptr && *endptr == '\0');
}

// Closes both ends of the given pipe
void close_pipe(int *pipe){
    if (pipe == NULL) {
        return;
    }
    close(pipe[0]);
    close(pipe[1]);
}

void prompt() {
    while (1) {
        // Wait on children processes
		wait_on_children();

		// Print status updates
		print_status_updates();

        // Write prompt
        const char *prompt_text= "$ ";
        write(1, prompt_text, strlen(prompt_text));
     
		// Read shell input
        int nextAddNewLine;
        char *cmd = get_input(&nextAddNewLine);
        
        // If EOF with no characters, exits
        if (cmd == NULL && !wasInterrupted) {
            vec_destroy(&background_status_updates);
            vec_destroy(&stopped_background_jobs);
            vec_destroy(&background_jobs);
            unmount();
            return;
        }

        // Continue loop on ctr-C interrupt
        if (wasInterrupted) {
            wasInterrupted = 0;
            continue;
        }

        // Add new line if necessary
        if (nextAddNewLine) {
            write(1, "\n", 1);
        }

        // Parse command
        struct parsed_command *parsed_cmd;
        int parse_res = parse_command(cmd, &parsed_cmd);

        // Execute commands if valid
        if (parse_res != 0) {
            printf("parse_command: invalid command\n");
            free(cmd);
        } else {
            execute_commands(parsed_cmd, cmd);
            free(cmd);
        }
    }
}

void print_status_updates() {
	while (!vec_is_empty(&background_status_updates)) {
		void *next_update;
		if (!vec_pop_back(&background_status_updates, &next_update)) {
            write(2, "no job with job_id exists\n", strlen("no job with job_id exists\n"));
			return;
		}

		char *to_write_status = str_concat((char*) next_update, "\n");
		write(2,  to_write_status, strlen(to_write_status));

		free(to_write_status);
		free(next_update);
	}
}

char *get_input(int *nextAddNewLine) {
    int num_bufs = 0;
    *nextAddNewLine = 0;
    char *buffer = malloc((BUF_SIZE+1) * sizeof(char));
    if (buffer == NULL) { return NULL; }
    char *cmd = buffer;

    do {
        int num_read = read(0, buffer, BUF_SIZE);
        if (wasInterrupted) {
            free(cmd);
            return NULL;
        }

        // Handle error or first char EOF
        if (num_read == -1) { free(cmd); return NULL; }
        if (num_read == 0) {
            if (cmd == buffer) {
                write(1, "\n", 1);
                free(cmd);
                return NULL;
            }
            *nextAddNewLine = 1;
            break;
        }

        // Add terminating null
        buffer[num_read] = '\0';

        // Handle partially full buffer
        if (num_read < BUF_SIZE) {
            if (buffer[num_read-1] == '\n') { buffer[num_read-1] = '\0'; }
            else { *nextAddNewLine = 1; }

            break;
        }

        // Handle full buffer with terminating new line
        if (buffer[num_read-1] == '\n') {
            *nextAddNewLine = 0;
            buffer[num_read-1] = '\0';
            break;
        }

        // Increase buffer size for next read
        num_bufs++;
        cmd = realloc(cmd, (num_bufs+1)*BUF_SIZE+1);
        if (cmd == NULL) { return NULL; }
        buffer = cmd + (ptrdiff_t)((num_bufs)*BUF_SIZE);
    } while (1);

    return cmd;
}

void wait_on_children() {
	int status;
	int status_update_pid;
	
	// Wait on every child process with status update
	while ((status_update_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		int job_idx;
		job *updated_job = get_job_by_pid(status_update_pid, &job_idx, &background_jobs);

        if (updated_job == NULL) {
            continue;
        }

		if (WIFSTOPPED(status)) {
            updated_job->num_procs_stopped++;
		} else if (WIFEXITED(status) || WIFSIGNALED(status)) {
			updated_job->num_procs_running--;
		}

        if (updated_job->num_procs_running == 0) {
            // TODO: update message
            vec_push_back(&background_status_updates, str_concat("Finished: ", updated_job->full_cmd));
            vec_remove_job_by_id(&stopped_background_jobs, updated_job->id);
            vec_erase(&background_jobs, job_idx);
        } else if (updated_job->num_procs_stopped == updated_job->num_procs_running) {
            if (updated_job->status != STOPPED_STATE) {
                // TODO: update message
                updated_job->status = STOPPED_STATE;
                vec_push_back(&background_status_updates, str_concat("\nStopped: ", updated_job->full_cmd));
                vec_push_back(&stopped_background_jobs, updated_job);
            }
        }
	}
}

void execute_commands(struct parsed_command *parsed_cmd, char *cmd) {
    // exit if no commands
    if (parsed_cmd->num_commands == 0) {
        free(parsed_cmd);
        return;
    }

	// Handle jobs, bg, and fg if applicable
    if (parsed_cmd->num_commands == 1 && handle_job_builtins(parsed_cmd) == 1) {
        free(parsed_cmd);
        return;
    }

	// Setup new job for this command
    job *newJob = malloc(sizeof(job));
    newJob->id = ++curr_job_id;
    newJob->cmd = parsed_cmd;
    newJob->pids = malloc(sizeof(pid_t)*parsed_cmd->num_commands+1);
    newJob->full_cmd = str_copy(cmd);
    newJob->num_procs_running = 0;
    newJob->status = RUNNING_STATE;
	newJob->num_procs_stopped = 0;

    if (parsed_cmd->num_commands == 1) {
        // if only one command, fork and exec
        pid_t pid = fork();

        // Exec child
        if (pid == 0) {
            setpgid(0, 0);
            prepare_child_process(parsed_cmd);

            if (parsed_cmd->stdin_file != NULL) {
                int fd = open(parsed_cmd->stdin_file, O_RDONLY);
                if (fd) {
                    perror("failed to open file.");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                err_t err = close(fd);
                if (err) {
                    perror("failed to close file");
                    exit(EXIT_FAILURE);
                }
            }
            if (parsed_cmd->stdout_file != NULL) {
                int fd = open(parsed_cmd->stdout_file, O_WRONLY);
                if (fd) {
                    perror("failed to open file.");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                err_t err = close(fd);
                if (err) {
                    perror("failed to close file");
                    exit(EXIT_FAILURE);
                }

            }

            execvp(parsed_cmd->commands[0][0], parsed_cmd->commands[0]);
            perror("execvp");
            exit(EXIT_FAILURE);
        }

        // Set child to new process group
        if (setpgid(pid, pid) == -1) {
            perror("setpgid of child error\n");
            exit(EXIT_FAILURE);
        }
        
        // Setup child in new job
        newJob->pids[0] = pid;
        newJob->num_procs_running += 1;
    } else {
        // if more than one child, handle pipes
        int (*all_pipes)[2] = calloc((parsed_cmd->num_commands - 1), sizeof(*all_pipes));

        int pgid = 0;
        for (int i=0; i < parsed_cmd->num_commands; i++) {
            // We don't need a pipe for the last command
            if (i != parsed_cmd->num_commands - 1) {
                pipe2(all_pipes[i], O_CLOEXEC);
            }

            pid_t command_pid = fork();
            if (i == 0) {
                if (command_pid != 0) {
                    pgid = command_pid;
                }
            }

            if (command_pid == 0) {
                prepare_child_process(parsed_cmd);
                if (setpgid(0, pgid) == -1) {
                    perror("setpgid of child error\n");
                    exit(EXIT_FAILURE);
                }

                // If we aren't the first pipe, STDIN should come from the LAST pipe's read end
                if (i != 0) {
                    dup2(all_pipes[i-1][READ_END], STDIN_FILENO);
                } else {
                    if (parsed_cmd->stdin_file != NULL) {
                        int fd = open(parsed_cmd->stdin_file, O_RDONLY);
                        if (fd) {
                            perror("failed to open file.");
                            exit(EXIT_FAILURE);
                        }
                        dup2(fd, STDIN_FILENO);
                        err_t err = close(fd);
                        if (err) {
                            perror("failed to close file");
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                // If we aren't the last pipe, STDOUT should go to from THIS pipe's write end
                if (i != parsed_cmd->num_commands - 1) {
                    dup2(all_pipes[i][WRITE_END], STDOUT_FILENO);
                } else {
                    int fd = open(parsed_cmd->stdout_file, O_WRONLY);
                    if (fd) {
                        perror("failed to open file.");
                        exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDOUT_FILENO);
                    err_t err = close(fd);
                    if (err) {
                        perror("failed to close file");
                        exit(EXIT_FAILURE);
                    }
                }

                execvp(parsed_cmd->commands[i][0], parsed_cmd->commands[i]);
                exit(EXIT_FAILURE);
            }

            // Set child to new process group
            if (setpgid(command_pid, pgid) == -1) {
                perror("setpgid of child error\n");
                exit(EXIT_FAILURE);
            }

            newJob->pids[i] = command_pid;
            newJob->num_procs_running++;
        }

        for (int i=0; i < parsed_cmd->num_commands - 1; i++) {
            close_pipe(all_pipes[i]);
        }
        free(all_pipes);
    }

    vec_push_back(&background_jobs, newJob);
    if (parsed_cmd->is_background) {
        // If background job, update command name and add status update
        char *temp_cmd_ptr = newJob->full_cmd;
        while (*temp_cmd_ptr != '&') {
            temp_cmd_ptr++;
        }
        *temp_cmd_ptr = '\0';

        char *status_update = str_concat("Running: ", cmd);
        vec_push_back(&background_status_updates, status_update);
		curr_job_id++;
    } else {
        // handle if fg job
        start_fg_job(newJob);
    }
}

void prepare_child_process(struct parsed_command *parsed_cmd) {
    sigset_t mask;
    sigemptyset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    if (parsed_cmd->stdin_file != NULL) {
        changeStdInput(parsed_cmd);
    }

    if (parsed_cmd->stdout_file != NULL) {
        changeStdOutput(parsed_cmd);
    }
}

void start_fg_job(job *newJob) {
	// Declare as foreground job and get pgid
    newJob->cmd->is_background = 0;
    newJob->num_procs_stopped = 0;
    int pgid = newJob->pids[0];

	// Give job terminal control
    if (tcsetpgrp(STDIN_FILENO, pgid)) {
        perror("tcsetpgrp");
    }

	// If job is stopped, start it
    if (newJob->status == STOPPED_STATE) {
		newJob->status = RUNNING_STATE;
		kill(-pgid, SIGCONT);
	}

    char *status_update = NULL;
    int status;
    while (waitpid(-pgid, &status, WUNTRACED) > 0) {
		if (WIFSTOPPED(status)) {
			newJob->num_procs_stopped++;
		} else if (WIFEXITED(status) || WIFSIGNALED(status)) {
			newJob->num_procs_running--;
		}

		// Remove job if terminated
		if (newJob->num_procs_running == 0) {
            vec_remove_job_by_id(&stopped_background_jobs, newJob->id);
			vec_remove_job_by_id(&background_jobs, newJob->id);
			break;
		} 

		// If all processes stopped, set to stopped state

		if (newJob->num_procs_running == newJob->num_procs_stopped) {
			newJob->status = STOPPED_STATE;

			vec_push_back(&stopped_background_jobs, newJob);
			status_update = str_concat("Stopped: ", newJob->full_cmd);
			vec_push_back(&background_status_updates, str_concat(status_update, "\n"));
			free(status_update);

			break;
		} 
	}

	// Give terminal control back to shell
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}

int handle_job_builtins(struct parsed_command *parsed_cmd) {
	// For all jobs, print relevant information
    if (strcmp(parsed_cmd->commands[0][0], "jobs") == 0) {
        for (int i = 0; i < vec_len(&background_jobs); i++) {
            job *curr_job = vec_get(&background_jobs, i);

            char *curr_job_status;
            if (curr_job->status == RUNNING_STATE) {
                curr_job_status = "running";
            } else {
                curr_job_status = "stopped";
            }

            printf("[%u] %s (%s)\n", curr_job->id, curr_job->full_cmd, curr_job_status);
        }
        return 1;
    }
    

	// Restart job in background and print status
    if (strcmp(parsed_cmd->commands[0][0], "bg") == 0) {
		// Get job to bg
		job *job_to_continue = get_job_bg_fg(parsed_cmd->commands[0][1], &stopped_background_jobs, &background_jobs);
		if (job_to_continue == NULL) {
			return 1;
		}
		
		if (job_to_continue->status == RUNNING_STATE) {
			perror("job already running.");
			return 1;
		}
		job_to_continue->num_procs_stopped = 0;
		job_to_continue->status = RUNNING_STATE;
        vec_remove_job_by_id(&stopped_background_jobs, job_to_continue->id);
        kill(-job_to_continue->pids[0], SIGCONT);
        printf("Running: %s\n", job_to_continue->full_cmd);

        return 1;
    }

    if (strcmp(parsed_cmd->commands[0][0], "fg") == 0) {
		// Get job to fg
		job *job_to_continue = get_job_bg_fg(parsed_cmd->commands[0][1], &stopped_background_jobs, &background_jobs);
		if (job_to_continue == NULL) {
			return 1;
		}

        if (job_to_continue->status == STOPPED_STATE) {
            char *message = str_concat("Restarting: ", job_to_continue->full_cmd);
            char *toPrint = str_concat(message, "\n");
            free(message);
            write(1, toPrint, strlen(toPrint));
            free(toPrint);
        } else {
            char *message = str_concat(job_to_continue->full_cmd, "\n");
            write(1, message, strlen(message));
            free(message);
        }

        vec_remove_job_by_id(&stopped_background_jobs, job_to_continue->id);
        start_fg_job(job_to_continue);

        return 1;
    }

    return 0;
}

int execvp(const char *cmd, char **args) {
    if (strcmp(cmd, "cat") == 0) {
        char **commands = (char**) args+1;
        int flag = 0;
        char *output_file = NULL;

        // Gets output flag and file if applicable
        int i = 0;
        while (commands[i] != NULL) {
            if (commands[i][0] == '-') {
                if (commands[i+1] == NULL) {
                    return 0;
                }

                if (commands[i][1] == 'a') {
                    flag = O_WRONLY | O_APPEND;
                } else if (commands[i][1] == 'w') {
                    flag = O_WRONLY;
                } else {
                    return 1;
                }
                output_file = commands[i+1];
                commands[i] = NULL;
                break;
            }
            i++;
        }

        cat(commands + 1, output_file, flag);
    } else if (strcmp(cmd, "ls") == 0) {
        ls(args[1], 0);
    } else if (strcmp(cmd, "touch") == 0) {
        touch(args+1);
    } else if (strcmp(cmd, "mv") == 0) {
        if (args[1] == NULL || args[2] == NULL) {
            exit(-1);
        }
        mv(args[1], args[2]);
    } else if (strcmp(cmd, "rm") == 0) {
        rm(args+1);
    } else if (strcmp(cmd, "cp") == 0) {
        if (args[1] == NULL || args[2] == NULL) {
            exit(-1);
        }
        cp(args[1], args[2], 0);
    } else if (strcmp(cmd, "mkdir") == 0) {
        fs_mkdir(args+1);
    } else if (strcmp(cmd, "cd") == 0) {
        cd(args[1]);
    } else if (strcmp(cmd, "echo") == 0) {
        echo(args+1);
    }

    exit(EXIT_SUCCESS);
}
