#pragma once

#include <stdint.h>

#include "traps/traps.h"

// Initialize the timer with given hz
void timer_init(uint32_t tick_hz);

// Enable the periodic scheduler timer interrupt.
void timer_enable_scheduler_tick(void);

// Disable the periodic scheduler timer interrupt.
void timer_disable_scheduler_tick(void);

// Get current timer tick count
uint64_t timer_get_ticks(void);

// Get timer tick frequency in Hz
uint32_t timer_get_tick_hz(void);

// Get timer frequency in Hz
uint64_t timer_get_frequency(void);

// Busy-wait delay for given number of milliseconds.
void timer_delay_ms(uint64_t milliseconds);

// Weak handler for timer interrupt, strong definition in scheduler.c
void scheduler_tick(struct trap_frame *frame);
