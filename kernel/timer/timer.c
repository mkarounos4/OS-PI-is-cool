#include "timer.h"

#include "irq/irq.h"
#include "scheduler/process.h"
#include "scheduler/scheduler.h"

#define ARM_GENERIC_TIMER_INTID 30u
#define CNTP_CTL_ENABLE        1u
#define TIMER_MAX_TVAL         UINT64_C(0xffffffff)
#define MAX_SOFTWARE_TIMERS    64u

static volatile uint64_t timer_ticks;
static uint64_t timer_frequency;

typedef struct software_timer_st {
    uint8_t active;
    uint64_t deadline;
    timer_handler_t handler;
    void *ctx;
} software_timer_t;

static software_timer_t software_timers[MAX_SOFTWARE_TIMERS];

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

static uint64_t timer_ms_to_counter_ticks(uint64_t milliseconds) {
    uint64_t frequency = timer_frequency;

    if (frequency == 0) {
        frequency = read_cntfrq_el0();
        timer_frequency = frequency;
    }

    uint64_t ticks =
        (frequency / 1000u) * milliseconds +
        ((frequency % 1000u) * milliseconds) / 1000u;

    return ticks == 0 ? 1 : ticks;
}

static void timer_rearm_locked(void) {
    uint64_t now = read_cntpct_el0();
    uint64_t next_deadline = 0;
    int found = 0;

    for (unsigned i = 0; i < MAX_SOFTWARE_TIMERS; i++) {
        if (!software_timers[i].active) {
            continue;
        }

        if (!found || (int64_t)(software_timers[i].deadline - next_deadline) < 0) {
            next_deadline = software_timers[i].deadline;
            found = 1;
        }
    }

    if (!found) {
        write_cntp_ctl_el0(0);
        return;
    }

    uint64_t delay = 1;
    if ((int64_t)(next_deadline - now) > 0) {
        delay = next_deadline - now;
    }
    if (delay > TIMER_MAX_TVAL) {
        delay = TIMER_MAX_TVAL;
    }

    write_cntp_tval_el0((uint32_t)delay);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
}

static struct trap_frame *timer_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx) {
    timer_handler_t expired_handlers[MAX_SOFTWARE_TIMERS];
    void *expired_ctxs[MAX_SOFTWARE_TIMERS];
    unsigned expired_count = 0;
    uint64_t now;

    (void)intid;
    (void)ctx;

    write_cntp_ctl_el0(0);
    timer_ticks++;
    now = read_cntpct_el0();

    for (unsigned i = 0; i < MAX_SOFTWARE_TIMERS; i++) {
        if (!software_timers[i].active) {
            continue;
        }

        if ((int64_t)(now - software_timers[i].deadline) >= 0) {
            expired_handlers[expired_count] = software_timers[i].handler;
            expired_ctxs[expired_count] = software_timers[i].ctx;
            expired_count++;
            software_timers[i].active = 0;
        }
    }

    for (unsigned i = 0; i < expired_count; i++) {
        if (expired_handlers[i] != 0) {
            expired_handlers[i](expired_ctxs[i]);
        }
    }

    timer_rearm_locked();
    return frame;
}

void timer_init(void) {
    write_cntp_ctl_el0(0);
    timer_frequency = read_cntfrq_el0();
    for (unsigned i = 0; i < MAX_SOFTWARE_TIMERS; i++) {
        software_timers[i].active = 0;
    }
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
    uint64_t delay_ticks;
    uint64_t flags;
    int slot = -1;

    if (handler == 0) {
        return -1;
    }

    delay_ticks = timer_ms_to_counter_ticks(milliseconds);

    flags = irq_save();
    for (unsigned i = 0; i < MAX_SOFTWARE_TIMERS; i++) {
        if (!software_timers[i].active) {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }

    software_timers[slot].deadline = read_cntpct_el0() + delay_ticks;
    software_timers[slot].handler = handler;
    software_timers[slot].ctx = ctx;
    software_timers[slot].active = 1;
    timer_rearm_locked();
    irq_restore(flags);

    return 0;
}

void timer_cancel_interrupt(void) {
    uint64_t flags = irq_save();

    for (unsigned i = 0; i < MAX_SOFTWARE_TIMERS; i++) {
        software_timers[i].active = 0;
    }
    timer_rearm_locked();
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

static void timer_wake_process(void *ctx) {
    pid_t pid = (pid_t)(uintptr_t)ctx;
    pcb_t *pcb = get_pcb_by_pid(pid);

    if (pcb != NULL) {
        unblock_process(pcb);
    }
}

long timer_sleep_ms(uint64_t milliseconds) {
    pcb_t *pcb = get_curr_process();

    if (pcb == 0) {
        return SYS_ESRCH;
    }

    if (milliseconds == 0) {
        schedule_yield();
        return 0;
    }

    pcb->blocked_until |= BLOCK_UNTIL_TIMER;
    if (timer_schedule_interrupt_ms(milliseconds, timer_wake_process,
                                    (void *)(uintptr_t)pcb->pid) != 0) {
        pcb->blocked_until &= ~BLOCK_UNTIL_TIMER;
        return SYS_EAGAIN;
    }

    block_process(pcb);
    return 0;
}
