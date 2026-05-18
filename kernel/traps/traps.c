#include "traps.h"

#include "irq/irq.h"
#include "uart/uart.h"

#define ESR_EC_SHIFT 26u
#define ESR_EC_MASK  UINT64_C(0x3f)
#define ESR_EC_BRK64 UINT64_C(0x3c)
#define BRK_SELFTEST UINT64_C(0x42)

extern char exception_vectors[];

// Converts interrupt type to a human-readable string for debugging purposes. .
static const char *exception_type_name(uint64_t type) {
    switch (type) {
    case EXC_SYNC_CURRENT_SP0:
        return "sync current SP0";
    case EXC_IRQ_CURRENT_SP0:
        return "irq current SP0";
    case EXC_FIQ_CURRENT_SP0:
        return "fiq current SP0";
    case EXC_SERROR_CURRENT_SP0:
        return "serror current SP0";
    case EXC_SYNC_CURRENT_SPX:
        return "sync current SPx";
    case EXC_IRQ_CURRENT_SPX:
        return "irq current SPx";
    case EXC_FIQ_CURRENT_SPX:
        return "fiq current SPx";
    case EXC_SERROR_CURRENT_SPX:
        return "serror current SPx";
    case EXC_SYNC_LOWER_A64:
        return "sync lower A64";
    case EXC_IRQ_LOWER_A64:
        return "irq lower A64";
    case EXC_FIQ_LOWER_A64:
        return "fiq lower A64";
    case EXC_SERROR_LOWER_A64:
        return "serror lower A64";
    case EXC_SYNC_LOWER_A32:
        return "sync lower A32";
    case EXC_IRQ_LOWER_A32:
        return "irq lower A32";
    case EXC_FIQ_LOWER_A32:
        return "fiq lower A32";
    case EXC_SERROR_LOWER_A32:
        return "serror lower A32";
    default:
        return "unknown";
    }
}

// True iff the given exception type is an IRQ exception
static int is_irq_type(uint64_t type) {
    return type == EXC_IRQ_CURRENT_SP0 ||
           type == EXC_IRQ_CURRENT_SPX ||
           type == EXC_IRQ_LOWER_A64 ||
           type == EXC_IRQ_LOWER_A32;
}

// True iff the given exception type is a synchronous exception
static int is_sync_type(uint64_t type) {
    return type == EXC_SYNC_CURRENT_SP0 ||
           type == EXC_SYNC_CURRENT_SPX ||
           type == EXC_SYNC_LOWER_A64 ||
           type == EXC_SYNC_LOWER_A32;
}

// Prints the given trap frame to the console for debugging purposes
static void print_frame(const struct trap_frame *frame) {
    uart_puts("type: ");
    uart_puts(exception_type_name(frame->type));
    uart_puts("\nESR:  ");
    uart_puthex(frame->esr);
    uart_puts("\nELR:  ");
    uart_puthex(frame->elr);
    uart_puts("\nFAR:  ");
    uart_puthex(frame->far);
    uart_puts("\nSPSR: ");
    uart_puthex(frame->spsr);
    uart_puts("\nSP:   ");
    uart_puthex(frame->sp);
    uart_puts("\nX0:   ");
    uart_puthex(frame->regs[0]);
    uart_puts("\nX1:   ");
    uart_puthex(frame->regs[1]);
    uart_puts("\nX2:   ");
    uart_puthex(frame->regs[2]);
    uart_puts("\nX30:  ");
    uart_puthex(frame->regs[30]);
    uart_puts("\n");
}

// Prints frame then halts CPU for fatal exceptions.
static void __attribute__((noreturn)) exception_halt(const struct trap_frame *frame) {
    irq_disable();
    uart_puts("\nFatal exception\n");
    print_frame(frame);

    while (1) {
        asm volatile("wfe");
    }
}

void exceptions_init(void) {
    irq_disable();
    asm volatile(
        "msr vbar_el1, %0\n"
        "dsb sy\n"
        "isb\n"
        :
        : "r"(exception_vectors)
        : "memory");
}

uint64_t cpu_current_el(void) {
    uint64_t current_el;
    asm volatile("mrs %0, CurrentEL" : "=r"(current_el));
    return (current_el >> 2) & UINT64_C(0x3);
}

void irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

void irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}

uint64_t irq_save(void) {
    uint64_t flags;
    asm volatile("mrs %0, daif" : "=r"(flags));
    irq_disable();
    return flags;
}

void irq_restore(uint64_t flags) {
    asm volatile("msr daif, %0" : : "r"(flags) : "memory");
}

struct trap_frame *exception_dispatch(struct trap_frame *frame) {
    uint64_t ec = (frame->esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    uint64_t iss = frame->esr & UINT64_C(0xffffff);

    if (is_irq_type(frame->type)) {
        return irq_handle_exception(frame);
    }

    if (is_sync_type(frame->type) && ec == ESR_EC_BRK64 && iss == BRK_SELFTEST) {
        uart_puts("BRK exception self-test passed\n");
        frame->elr += 4;
        return frame;
    }

    exception_halt(frame);
}
