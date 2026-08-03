#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT {0}

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
uint64_t spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags);

