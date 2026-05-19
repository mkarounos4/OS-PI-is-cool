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
    uint64_t regs[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
    uint64_t type;
    uint64_t intid;
};

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
