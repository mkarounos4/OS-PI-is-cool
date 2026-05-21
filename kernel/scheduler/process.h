#pragma once

#include <stdint.h>

#include "data-structs/vec.h"
#include "memory/malloc.h"
#include "memory/virt/vmm.h"
#include "traps/traps.h"

typedef int32_t pid_t;

#define MAX_PROCESS_COUNT 16
#define PROC_STACK_SIZE 2048u
#define PROC_HEAP_SIZE 16384u

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
    
    const char *name;
    pid_t waiting_for_pid;
    uintptr_t wait_status_ptr;
    enum process_state state;
    int exit_code;

    // Thread parameters (implementation simplified to one thread per process)
    struct trap_frame *frame;
    unsigned char user_stack[PROC_STACK_SIZE];
    unsigned char kernel_stack[PROC_STACK_SIZE];
    unsigned char heap[PROC_HEAP_SIZE];
    
    uintptr_t user_stack_base;
    uintptr_t user_stack_top;
    uintptr_t kernel_stack_base;
    uintptr_t kernel_stack_top;
    
    struct mem_ctx heap_ctx;
    struct address_space *as;
    uint64_t user_code_base;
    uint64_t user_heap_base;
    uint64_t user_heap_brk;
    uint64_t user_heap_end;

    Vec children;   // vec of children PIDs
    Vec file_descriptors;   // vec of fds
} pcb_t;

pcb_t *get_pcb_by_pid(pid_t pid);
void processes_init();
pid_t proc_create(void *(*func)(void*), void *args, pid_t ppid);
