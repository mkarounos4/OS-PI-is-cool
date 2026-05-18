#include "timer.h"

#include "irq/irq.h"

#define ARM_GENERIC_TIMER_INTID 30u
#define CNTP_CTL_ENABLE        1u
#define TIMER_MAX_TVAL         UINT64_C(0xffffffff)

static volatile uint64_t timer_ticks;
static uint64_t timer_frequency;
static timer_handler_t timer_handler;
static void *timer_handler_ctx;

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

static struct trap_frame *timer_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx) {
    timer_handler_t handler = timer_handler;
    void *handler_ctx = timer_handler_ctx;

    (void)intid;
    (void)ctx;

    write_cntp_ctl_el0(0);
    timer_ticks++;

    if (handler == 0) {
        return 0;
    }

    return handler(frame, handler_ctx);
}

void timer_init(void) {
    write_cntp_ctl_el0(0);
    timer_frequency = read_cntfrq_el0();
    timer_handler = 0;
    timer_handler_ctx = 0;
    irq_register(ARM_GENERIC_TIMER_INTID, timer_irq_handler, 0);
    irq_enable_line(ARM_GENERIC_TIMER_INTID);
}

uint64_t timer_get_ticks(void) {
    uint64_t flags = irq_save();
    uint64_t ticks = timer_ticks;
    irq_restore(flags);

    return ticks;
}

uint64_t timer_get_frequency(void) {
    return timer_frequency;
}

int timer_schedule_interrupt_ms(uint64_t milliseconds, timer_handler_t handler, void *ctx) {
    uint64_t frequency = timer_frequency;
    uint64_t delay_ticks;
    uint64_t flags;

    if (handler == 0) {
        return -1;
    }

    if (frequency == 0) {
        frequency = read_cntfrq_el0();
        timer_frequency = frequency;
    }

    delay_ticks =
        (frequency / 1000u) * milliseconds +
        ((frequency % 1000u) * milliseconds) / 1000u;

    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    if (delay_ticks > TIMER_MAX_TVAL) {
        return -1;
    }

    flags = irq_save();
    timer_handler = handler;
    timer_handler_ctx = ctx;
    write_cntp_tval_el0((uint32_t)delay_ticks);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
    irq_restore(flags);

    return 0;
}

void timer_cancel_interrupt(void) {
    uint64_t flags = irq_save();

    write_cntp_ctl_el0(0);
    timer_handler = 0;
    timer_handler_ctx = 0;
    irq_restore(flags);
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
