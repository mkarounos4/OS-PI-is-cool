#include "spinlock.h"

#include "traps/traps.h"

void spin_lock(spinlock_t *lock) {
    uint32_t tmp;
    uint32_t status;
    volatile uint32_t *value = &lock->value;

    asm volatile(
        "sevl\n"
        "1: wfe\n"
        "2: ldaxr %w0, [%2]\n"
        "cbnz %w0, 1b\n"
        "mov %w0, #1\n"
        "stxr %w1, %w0, [%2]\n"
        "cbnz %w1, 2b\n"
        : "=&r"(tmp), "=&r"(status)
        : "r"(value)
        : "memory");
}

void spin_unlock(spinlock_t *lock) {
    asm volatile("stlr wzr, [%0]\nsev" : : "r"(&lock->value) : "memory");
}

uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    irq_restore(flags);
}
