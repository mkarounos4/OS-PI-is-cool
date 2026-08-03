#include "cpu.h"

#include "memory/page_table/page_table.h"
#include "sync/spinlock.h"

cpu_t cpus[MAX_CPUS];
uint8_t cpu_boot_stacks[MAX_CPUS][CPU_BOOT_STACK_SIZE] __attribute__((aligned(16)));

static spinlock_t cpu_lock = SPINLOCK_INIT;
static volatile uint32_t cpu_started[MAX_CPUS];
static volatile uint32_t cpu_release_prepared;

extern const uint64_t secondary_start_phys;

#define QEMU_SPINTABLE_BASE UINT64_C(0xd8)
#define RPI5_TM_ENTRYPOINT  UINT64_C(0x100)
#define RPI5_TM_HOLD_BASE   UINT64_C(0x108)
#define RPI5_TM_HOLD_GO     UINT64_C(1)

static volatile uint64_t *boot_word(uint64_t pa) {
    return (volatile uint64_t *)(uintptr_t)(KERNEL_VA_BASE | pa);
}

uint32_t cpu_id_from_mpidr(uint64_t mpidr) {
    return (uint32_t)(mpidr & UINT64_C(0xff));
}

uint32_t cpu_id(void) {
    cpu_t *cpu = cpu_current();
    if (cpu == 0) {
        return 0;
    }
    return cpu->id;
}

cpu_t *cpu_get(uint32_t id) {
    if (id >= MAX_CPUS) {
        return 0;
    }
    return &cpus[id];
}

void cpu_early_init(uint32_t id, uint64_t mpidr) {
    if (id >= MAX_CPUS) {
        id = 0;
    }

    cpu_t *cpu = &cpus[id];
    cpu->id = id;
    cpu->mpidr = mpidr;
    cpu->ready_to_schedule = 0;
    cpu->ready_ctx = 0;
    cpu->curr_thread = 0;
    cpu->scheduler_ticks = 0;
    cpu_set_current(cpu);
}

void cpu_mark_online(uint32_t id) {
    if (id >= MAX_CPUS) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&cpu_lock);
    cpus[id].online = 1;
    spin_unlock_irqrestore(&cpu_lock, flags);
    asm volatile("sev" ::: "memory");
}

uint32_t cpu_online_count(void) {
    uint32_t count = 0;
    uint64_t flags = spin_lock_irqsave(&cpu_lock);
    for (uint32_t i = 0; i < MAX_CPUS; i++) {
        if (cpus[i].online) {
            count++;
        }
    }
    spin_unlock_irqrestore(&cpu_lock, flags);
    return count;
}

void cpu_wait_until_online(uint32_t id) {
    if (id >= MAX_CPUS) {
        return;
    }

    while (!cpus[id].online) {
        asm volatile("wfe" ::: "memory");
    }
}

void cpu_wait_until_started(uint32_t id) {
    if (id >= MAX_CPUS) {
        return;
    }

    while (!cpu_started[id]) {
        asm volatile("wfe" ::: "memory");
    }
}

void cpu_prepare_secondary_cores(void) {
    if (cpu_release_prepared) {
        return;
    }

    uint64_t entry = secondary_start_phys;
    uint64_t flags = spin_lock_irqsave(&cpu_lock);
    if (cpu_release_prepared) {
        spin_unlock_irqrestore(&cpu_lock, flags);
        return;
    }

#if defined(PLATFORM_QEMU)
    for (uint32_t id = 1; id < MAX_CPUS; id++) {
        *boot_word(QEMU_SPINTABLE_BASE + (id * sizeof(uint64_t))) = entry;
    }
#else
    *boot_word(RPI5_TM_ENTRYPOINT) = entry;
    for (uint32_t id = 1; id < MAX_CPUS; id++) {
        *boot_word(RPI5_TM_HOLD_BASE + (id * sizeof(uint64_t))) = RPI5_TM_HOLD_GO;
    }
#endif

    asm volatile("dsb sy\nsev" ::: "memory");
    cpu_release_prepared = 1;
    spin_unlock_irqrestore(&cpu_lock, flags);
}

void cpu_release_secondary_cores(void) {
    uint64_t flags = spin_lock_irqsave(&cpu_lock);

    for (uint32_t id = 1; id < MAX_CPUS; id++) {
        cpu_started[id] = 1;
    }

    asm volatile("dsb sy\nsev" ::: "memory");
    spin_unlock_irqrestore(&cpu_lock, flags);
}
