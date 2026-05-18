#include "timer.h"

#include "irq/irq.h"

#define ARM_GENERIC_TIMER_INTID 30u
#define CNTP_CTL_ENABLE        1u

static volatile uint64_t timer_ticks;
static uint64_t timer_interval;
static uint64_t timer_frequency;
static uint32_t timer_tick_hz;
static int scheduler_tick_enabled;

static inline uint64_t read_cntfrq_el0(void) {
    uint64_t value;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static inline uint64_t read_cntpct_el0(void) {
    uint64_t value;
    asm volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static inline void write_cntp_tval_el0(uint32_t value) {
    asm volatile("msr cntp_tval_el0, %0" : : "r"(value) : "memory");
}

static inline void write_cntp_ctl_el0(uint32_t value) {
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(value) : "memory");
}

static void program_next_tick(void) {
    write_cntp_tval_el0((uint32_t)timer_interval);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
}

static void timer_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx) {
    (void)intid;
    (void)ctx;

    timer_ticks++;
    program_next_tick();
    scheduler_tick(frame);
}

void timer_init(uint32_t tick_hz) {
    if (tick_hz == 0) {
        tick_hz = 100;
    }

    write_cntp_ctl_el0(0);

    timer_frequency = read_cntfrq_el0();
    timer_tick_hz = tick_hz;
    timer_interval = timer_frequency / tick_hz;

    if (timer_interval == 0) {
        timer_interval = 1;
    }
}

void timer_enable_scheduler_tick(void) {
    if (scheduler_tick_enabled) {
        return;
    }

    timer_ticks = 0;
    irq_register(ARM_GENERIC_TIMER_INTID, timer_irq_handler, 0);
    irq_enable_line(ARM_GENERIC_TIMER_INTID);
    scheduler_tick_enabled = 1;
    program_next_tick();
}

void timer_disable_scheduler_tick(void) {
    irq_disable_line(ARM_GENERIC_TIMER_INTID);
    write_cntp_ctl_el0(0);
    scheduler_tick_enabled = 0;
}

uint64_t timer_get_ticks(void) {
    uint64_t flags = irq_save();
    uint64_t ticks = timer_ticks;
    irq_restore(flags);

    return ticks;
}

uint32_t timer_get_tick_hz(void) {
    return timer_tick_hz;
}

uint64_t timer_get_frequency(void) {
    return timer_frequency;
}

void timer_delay_ms(uint64_t milliseconds) {
    uint64_t frequency = timer_frequency;

    if (frequency == 0) {
        frequency = read_cntfrq_el0();
    }

    uint64_t ticks =
        (frequency / 1000u) * milliseconds +
        ((frequency % 1000u) * milliseconds) / 1000u;
    uint64_t deadline = read_cntpct_el0() + ticks;

    while ((int64_t)(read_cntpct_el0() - deadline) < 0) {
        asm volatile("yield" ::: "memory");
    }
}

__attribute__((weak)) void scheduler_tick(struct trap_frame *frame) {
    (void)frame;
}
