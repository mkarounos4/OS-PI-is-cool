#pragma once

#include <stdint.h>

#include "traps/traps.h"

#define MAX_CPUS 4u
#define CPU_BOOT_STACK_SIZE 4096u

typedef struct thread_st tcb_t;

typedef struct cpu_st {
    uint32_t id;
    uint64_t mpidr;
    volatile uint32_t online;
    volatile uint32_t ready_to_schedule;
    void *ready_ctx;
    tcb_t *curr_thread;
    struct cpu_context boot_ctx;
    struct cpu_context idle_ctx;
    uint8_t *idle_stack;
    uint64_t scheduler_ticks;
} cpu_t;

extern cpu_t cpus[MAX_CPUS];
extern uint8_t cpu_boot_stacks[MAX_CPUS][CPU_BOOT_STACK_SIZE];

static inline cpu_t *cpu_current(void) {
    uint64_t value;
    asm volatile("mrs %0, tpidr_el1" : "=r"(value));
    return (cpu_t *)(uintptr_t)value;
}

static inline void cpu_set_current(cpu_t *cpu) {
    asm volatile("msr tpidr_el1, %0" : : "r"((uint64_t)(uintptr_t)cpu) : "memory");
}

uint32_t cpu_id(void);
uint32_t cpu_id_from_mpidr(uint64_t mpidr);
cpu_t *cpu_get(uint32_t id);
void cpu_early_init(uint32_t id, uint64_t mpidr);
void cpu_mark_online(uint32_t id);
uint32_t cpu_online_count(void);
void cpu_wait_until_online(uint32_t id);
void cpu_wait_until_started(uint32_t id);
void cpu_prepare_secondary_cores(void);
void cpu_release_secondary_cores(void);
