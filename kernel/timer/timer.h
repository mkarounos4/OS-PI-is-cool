#pragma once

#include <stdint.h>

#include "traps/traps.h"

typedef struct trap_frame *(*timer_handler_t)(struct trap_frame *frame, void *ctx);

// Initialize the generic timer hardware.
void timer_init(void);

// Schedule a one-shot timer interrupt for the given number of milliseconds away.
int timer_schedule_interrupt_ms(uint64_t milliseconds, timer_handler_t handler, void *ctx);

// Cancel any currently scheduled one-shot timer interrupt.
void timer_cancel_interrupt(void);

// Get number of timer interrupts that have fired.
uint64_t timer_get_ticks(void);

// Get timer frequency in Hz
uint64_t timer_get_frequency(void);

// Busy-wait delay for given number of milliseconds.
void timer_delay_ms(uint64_t milliseconds);
