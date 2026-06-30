#pragma once

#include <stdint.h>

enum exception_type {
    EXC_SYNC_CURRENT_SP0 = 0,
    EXC_IRQ_CURRENT_SP0 = 1,
    EXC_FIQ_CURRENT_SP0 = 2,
    EXC_SERROR_CURRENT_SP0 = 3,
    EXC_SYNC_CURRENT_SPX = 4,
    EXC_IRQ_CURRENT_SPX = 5,
    EXC_FIQ_CURRENT_SPX = 6,
    EXC_SERROR_CURRENT_SPX = 7,
    EXC_SYNC_LOWER_A64 = 8,
    EXC_IRQ_LOWER_A64 = 9,
    EXC_FIQ_LOWER_A64 = 10,
    EXC_SERROR_LOWER_A64 = 11,
    EXC_SYNC_LOWER_A32 = 12,
    EXC_IRQ_LOWER_A32 = 13,
    EXC_FIQ_LOWER_A32 = 14,
    EXC_SERROR_LOWER_A32 = 15,
};

struct trap_frame {
    // Restoration fields
    uint64_t regs[31];
    uint64_t sp; // Stack Pointer
    uint64_t elr; // PC to return to after eret
    uint64_t spsr; // SPSR_EL1, saved processor stte. Mode that eret returns to (exception lvl, stack mode, interrupt mask bits, condition flags).

    // Diagnosis fields
    uint64_t esr;  // Exception syndrome register (contains exception class, so what exception happened)
    uint64_t far;  // Fault addr register, stores address of failure for data/instruction aborts/page faults
    uint64_t type;  // Stores enum exception_type of exception
    uint64_t intid; // interrupt id
};

struct cpu_context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t x30;
    uint64_t sp;
    uint64_t ttbr0_el1;
    uint64_t ttbr0_el1_va;
};

void context_switch(struct cpu_context *old_ctx, struct cpu_context *new_ctx);
void save_curr_context(struct cpu_context *curr_ctx);
void context_switch_to(struct cpu_context *new_ctx);
void trap_frame_restore(struct trap_frame *frame) __attribute__((noreturn));

// Initialize exception vectors
void exceptions_init(void);

// Dispatch an exception and return the next trap frame to execute.
struct trap_frame *exception_dispatch(struct trap_frame *frame);

// Print a saved trap frame and decoded exception details.
void trap_frame_dump(const struct trap_frame *frame);

// Get current exception level (0-3)
uint64_t cpu_current_el(void);

// Enable IRQ exceptions. This is a no-op if already enabled.
void irq_enable(void);

// Disable IRQ exceptions. This is a no-op if already disabled.
void irq_disable(void);

// Saves current DAIF then disables IRQs
uint64_t irq_save(void);

// Restores previous interrupt mask state from flags returned by irq_save()
void irq_restore(uint64_t flags);

void fatal_exception(const char *reason);
