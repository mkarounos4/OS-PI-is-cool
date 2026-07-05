#pragma once

#include <stdint.h>

#include "data-structs/vec.h"
#include "traps/traps.h"
#include "fs/kapi.h"
#include "fs/cmds.h"
#include "errno.h"

#ifndef PID_T_DEFINED
#define PID_T_DEFINED
typedef int32_t pid_t;
#endif

#include "signals/signals.h"

#define MAX_PROCESS_COUNT 256
#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 4

#define ECHILD SYS_ECHILD

#define WAIT_EXITED 1
#define WAIT_SIGNALED 2
#define WAIT_STOPPED 3
#define WAIT_CONTINUED 4

#define BLOCK_UNTIL_NEW_CHILD 1
#define BLOCK_UNTIL_SIGNAL 2

enum process_state {
    PROC_UNUSED_STATE,
    PROC_READY_STATE,
    PROC_RUNNING_STATE,
    PROC_BLOCKED_STATE,
    PROC_STOPPED_STATE,
    PROC_ZOMBIE_STATE,
};

typedef struct pcb_st {
    pid_t pid; 
    pid_t ppid; // Parent pid
    pid_t pgid; // group id
    
    char name[32];
    pid_t waiting_for_pid;
    uint32_t waiting_for_flags;
    enum process_state state;
    int exit_code;
    uint32_t blocked_until;
    void *(*entry_func)(void*);
    void *args;

    // Thread parameters (implementation simplified to one thread per process)
    struct cpu_context ctx;

    uint8_t wait_stop_pending;
    uint8_t wait_cont_pending;

    Vec children;   // vec of children PIDs
    Vec file_descriptors;   // vec of fds
                            
    signalset_t mask;
    signalset_t pending_signals;
    
    struct sigaction sigactions[32];

    int priority;
} pcb_t;

typedef struct pgrp_st {
    Vec pids;
    int refcount;
    pid_t pgid;
} pgrp_t;

pcb_t *get_pcb_by_pid(pid_t pid);
pgrp_t *get_pgrp_by_pgid(pid_t pgid);
void processes_init();
pid_t proc_create(void *(*func)(void*), void *args, pid_t ppid);
void proc_destroy(pcb_t *p);
long s_waitpid_impl(pid_t pid, int *status, int32_t flags);
pid_t fork(struct trap_frame *frame);

void terminate_process(pcb_t *pcb);
void stop_process(pcb_t *pcb);
void block_process(pcb_t *pcb);
void unblock_process(pcb_t *pcb);
void continue_process(pcb_t *pcb);
void send_unblock_event(pid_t pid, uint32_t event);

pid_t getpgid();
int setpgrp(pid_t pid, pid_t pgid);
int dup2(int oldfd, int newfd);
int print_processes(int fd);
int set_process_name(const char *name);
int set_process_name_for_pid(pid_t pid, const char *name);
