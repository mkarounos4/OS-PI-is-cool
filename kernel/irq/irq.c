#include "irq.h"

#include <stddef.h>

#define GIC_MAX_INTIDS 1020u

#define RPI_GICD_BASE  UINT64_C(0x107fff9000)
#define RPI_GICC_BASE  UINT64_C(0x107fffa000)
#define QEMU_GICD_BASE UINT64_C(0x08000000)
#define QEMU_GICC_BASE UINT64_C(0x08010000)

#define GICD_CTLR        0x000u
#define GICD_TYPER       0x004u
#define GICD_IIDR        0x008u
#define GICD_IGROUPR     0x080u
#define GICD_ISENABLER   0x100u
#define GICD_ICENABLER   0x180u
#define GICD_ISPENDR     0x200u
#define GICD_ICPENDR     0x280u
#define GICD_ICACTIVER   0x380u
#define GICD_IPRIORITYR  0x400u
#define GICD_ITARGETSR   0x800u
#define GICD_ICFGR       0xC00u

#define GICC_CTLR        0x000u
#define GICC_PMR         0x004u
#define GICC_IAR         0x00Cu
#define GICC_EOIR        0x010u

#define GICC_IAR_INTID_MASK 0x3ffu
#define GICC_SPURIOUS_INTID 1023u

struct irq_slot {
    irq_handler_t handler;
    void *ctx;
};

static struct irq_slot irq_table[GIC_MAX_INTIDS];
static unsigned gic_lines;
static unsigned irq_depth;

extern uint64_t get_uart_base(void) __attribute__((weak));

static uint64_t gicd_base(void) {
    return get_uart_base != NULL ? RPI_GICD_BASE : QEMU_GICD_BASE;
}

static uint64_t gicc_base(void) {
    return get_uart_base != NULL ? RPI_GICC_BASE : QEMU_GICC_BASE;
}

static inline void mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static inline uint32_t mmio_read32(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}

static inline void gicd_write(uint32_t reg, uint32_t value) {
    mmio_write32(gicd_base() + reg, value);
}

static inline uint32_t gicd_read(uint32_t reg) {
    return mmio_read32(gicd_base() + reg);
}

static inline void gicc_write(uint32_t reg, uint32_t value) {
    mmio_write32(gicc_base() + reg, value);
}

static inline uint32_t gicc_read(uint32_t reg) {
    return mmio_read32(gicc_base() + reg);
}

static unsigned clamp_gic_lines(unsigned lines) {
    if (lines > GIC_MAX_INTIDS) {
        return GIC_MAX_INTIDS;
    }

    return lines;
}

void irq_init(void) {
    uint32_t typer;

    irq_disable();

    gicd_write(GICD_CTLR, 0);
    gicc_write(GICC_CTLR, 0);

    typer = gicd_read(GICD_TYPER);
    gic_lines = clamp_gic_lines(((typer & 0x1fu) + 1u) * 32u);

    for (unsigned intid = 0; intid < gic_lines; intid += 32u) {
        gicd_write(GICD_ICENABLER + (intid / 8u), 0xffffffffu);
        gicd_write(GICD_ICPENDR + (intid / 8u), 0xffffffffu);
        gicd_write(GICD_ICACTIVER + (intid / 8u), 0xffffffffu);
        gicd_write(GICD_IGROUPR + (intid / 8u), 0x00000000u);
    }

    for (unsigned intid = 0; intid < gic_lines; intid += 4u) {
        gicd_write(GICD_IPRIORITYR + intid, 0xa0a0a0a0u);
    }

    for (unsigned intid = 32; intid < gic_lines; intid += 4u) {
        gicd_write(GICD_ITARGETSR + intid, 0x01010101u);
    }

    for (unsigned intid = 0; intid < gic_lines; intid += 16u) {
        gicd_write(GICD_ICFGR + (intid / 4u), 0);
    }

    gicc_write(GICC_PMR, 0xffu);
    gicc_write(GICC_CTLR, 1u);
    gicd_write(GICD_CTLR, 1u);

    asm volatile("dsb sy\nisb" ::: "memory");
}

int irq_register(unsigned intid, irq_handler_t handler, void *ctx) {
    uint64_t flags;

    if (intid >= GIC_MAX_INTIDS || handler == NULL) {
        return -1;
    }

    flags = irq_save();
    irq_table[intid].handler = handler;
    irq_table[intid].ctx = ctx;
    irq_restore(flags);

    return 0;
}

void irq_enable_line(unsigned intid) {
    if (intid >= GIC_MAX_INTIDS) {
        return;
    }

    gicd_write(GICD_ISENABLER + ((intid / 32u) * 4u), 1u << (intid % 32u));
}

void irq_disable_line(unsigned intid) {
    if (intid >= GIC_MAX_INTIDS) {
        return;
    }

    gicd_write(GICD_ICENABLER + ((intid / 32u) * 4u), 1u << (intid % 32u));
}

void irq_force_pending(unsigned intid) {
    if (intid >= GIC_MAX_INTIDS) {
        return;
    }

    gicd_write(GICD_ISPENDR + ((intid / 32u) * 4u), 1u << (intid % 32u));
}

struct trap_frame *irq_handle_exception(struct trap_frame *frame) {
    uint32_t iar = gicc_read(GICC_IAR);
    unsigned intid = iar & GICC_IAR_INTID_MASK;

    frame->intid = intid;

    if (intid == GICC_SPURIOUS_INTID || intid >= GIC_MAX_INTIDS) {
        return frame;
    }

    irq_depth++;

    if (irq_table[intid].handler != NULL) {
        struct trap_frame *new_frame = irq_table[intid].handler(intid, frame, irq_table[intid].ctx);

        if (new_frame != NULL) {
            frame = new_frame;
        }
    } else {
        irq_disable_line(intid);
    }

    gicc_write(GICC_EOIR, iar);

    irq_depth--;

    return frame;
}

unsigned irq_get_depth(void) {
    return irq_depth;
}

void irq_get_controller_info(uint32_t *typer, uint32_t *iidr) {
    if (typer != NULL) {
        *typer = gicd_read(GICD_TYPER);
    }

    if (iidr != NULL) {
        *iidr = gicd_read(GICD_IIDR);
    }
}
