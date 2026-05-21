#include "traps.h"

#include <stddef.h>

#include "irq/irq.h"
#include "scheduler/scheduler.h"
#include "syscall/syscall.h"
#include "uart/uart.h"

#define TRAP_FRAME_SIZE          304u
#define ESR_EC_SHIFT             26u
#define ESR_EC_MASK              UINT64_C(0x3f)
#define ESR_ISS_MASK             UINT64_C(0xffffff)
#define ESR_FSC_MASK             UINT64_C(0x3f)
#define ESR_DABT_WNR             UINT64_C(1u << 6)
#define ESR_BRK_COMMENT_MASK     UINT64_C(0xffff)
#define ESR_EC_UNKNOWN           UINT64_C(0x00)
#define ESR_EC_SVC64             UINT64_C(0x15)
#define ESR_EC_SYSREG_TRAP       UINT64_C(0x18)
#define ESR_EC_IABT_LOWER        UINT64_C(0x20)
#define ESR_EC_IABT_CURRENT      UINT64_C(0x21)
#define ESR_EC_PC_ALIGN          UINT64_C(0x22)
#define ESR_EC_DABT_LOWER        UINT64_C(0x24)
#define ESR_EC_DABT_CURRENT      UINT64_C(0x25)
#define ESR_EC_SP_ALIGN          UINT64_C(0x26)
#define ESR_EC_BRK64             UINT64_C(0x3c)
#define BRK_SELFTEST             UINT64_C(0x42)

_Static_assert(sizeof(struct trap_frame) == TRAP_FRAME_SIZE,
               "vectors.S TRAP_FRAME_SIZE must match struct trap_frame");
_Static_assert(offsetof(struct trap_frame, regs) == 0,
               "trap_frame.regs offset changed");
_Static_assert(offsetof(struct trap_frame, sp) == 248,
               "trap_frame.sp offset changed");
_Static_assert(offsetof(struct trap_frame, elr) == 256,
               "trap_frame.elr offset changed");
_Static_assert(offsetof(struct trap_frame, spsr) == 264,
               "trap_frame.spsr offset changed");
_Static_assert(offsetof(struct trap_frame, esr) == 272,
               "trap_frame.esr offset changed");
_Static_assert(offsetof(struct trap_frame, far) == 280,
               "trap_frame.far offset changed");
_Static_assert(offsetof(struct trap_frame, type) == 288,
               "trap_frame.type offset changed");
_Static_assert(offsetof(struct trap_frame, intid) == 296,
               "trap_frame.intid offset changed");

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

// Converts ESR_EL1.EC to a short diagnostic name.
static const char *exception_class_name(uint64_t ec) {
    switch (ec) {
    case ESR_EC_UNKNOWN:
        return "unknown";
    case ESR_EC_SVC64:
        return "svc64";
    case ESR_EC_SYSREG_TRAP:
        return "system register trap";
    case ESR_EC_IABT_LOWER:
        return "instruction abort lower EL";
    case ESR_EC_IABT_CURRENT:
        return "instruction abort current EL";
    case ESR_EC_PC_ALIGN:
        return "pc alignment fault";
    case ESR_EC_DABT_LOWER:
        return "data abort lower EL";
    case ESR_EC_DABT_CURRENT:
        return "data abort current EL";
    case ESR_EC_SP_ALIGN:
        return "sp alignment fault";
    case ESR_EC_BRK64:
        return "brk64";
    default:
        return "unhandled exception class";
    }
}

// Converts common data/instruction abort fault-status codes to a diagnostic name.
static const char *fault_status_name(uint64_t fsc) {
    switch (fsc) {
    case 0x00:
        return "address size fault level 0";
    case 0x01:
        return "address size fault level 1";
    case 0x02:
        return "address size fault level 2";
    case 0x03:
        return "address size fault level 3";
    case 0x04:
        return "translation fault level 0";
    case 0x05:
        return "translation fault level 1";
    case 0x06:
        return "translation fault level 2";
    case 0x07:
        return "translation fault level 3";
    case 0x09:
        return "access flag fault level 1";
    case 0x0a:
        return "access flag fault level 2";
    case 0x0b:
        return "access flag fault level 3";
    case 0x0d:
        return "permission fault level 1";
    case 0x0e:
        return "permission fault level 2";
    case 0x0f:
        return "permission fault level 3";
    case 0x10:
        return "synchronous external abort";
    case 0x21:
        return "alignment fault";
    default:
        return "other fault status";
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

// True iff the given exception type is an FIQ exception
static int is_fiq_type(uint64_t type) {
    return type == EXC_FIQ_CURRENT_SP0 ||
           type == EXC_FIQ_CURRENT_SPX ||
           type == EXC_FIQ_LOWER_A64 ||
           type == EXC_FIQ_LOWER_A32;
}

// True iff the given exception type is an SError exception
static int is_serror_type(uint64_t type) {
    return type == EXC_SERROR_CURRENT_SP0 ||
           type == EXC_SERROR_CURRENT_SPX ||
           type == EXC_SERROR_LOWER_A64 ||
           type == EXC_SERROR_LOWER_A32;
}

static int is_abort_class(uint64_t ec) {
    return ec == ESR_EC_IABT_LOWER ||
           ec == ESR_EC_IABT_CURRENT ||
           ec == ESR_EC_DABT_LOWER ||
           ec == ESR_EC_DABT_CURRENT;
}

static int is_data_abort_class(uint64_t ec) {
    return ec == ESR_EC_DABT_LOWER || ec == ESR_EC_DABT_CURRENT;
}

static void print_hex_line(const char *label, uint64_t value) {
    uart_puts(label);
    uart_puthex(value);
    uart_putc('\n');
}

// Prints the given trap frame to the console for debugging purposes.
void trap_frame_dump(const struct trap_frame *frame) {
    uint64_t ec = (frame->esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    uint64_t iss = frame->esr & ESR_ISS_MASK;

    uart_puts("type: ");
    uart_puts(exception_type_name(frame->type));
    uart_puts("\n");
    uart_puts("EC:   ");
    uart_puts(exception_class_name(ec));
    uart_puts(" ");
    uart_puthex(ec);
    uart_puts("\n");
    print_hex_line("ISS:  ", iss);

    if (is_abort_class(ec)) {
        uint64_t fsc = iss & ESR_FSC_MASK;
        uart_puts("FSC:  ");
        uart_puts(fault_status_name(fsc));
        uart_puts(" ");
        uart_puthex(fsc);
        uart_puts("\n");

        if (is_data_abort_class(ec)) {
            uart_puts("DATA: ");
            uart_puts((iss & ESR_DABT_WNR) != 0 ? "write\n" : "read\n");
        }
    }

    print_hex_line("ESR:  ", frame->esr);
    print_hex_line("ELR:  ", frame->elr);
    print_hex_line("FAR:  ", frame->far);
    print_hex_line("SPSR: ", frame->spsr);
    print_hex_line("SP:   ", frame->sp);
    print_hex_line("X0:   ", frame->regs[0]);
    print_hex_line("X1:   ", frame->regs[1]);
    print_hex_line("X2:   ", frame->regs[2]);
    print_hex_line("X8:   ", frame->regs[8]);
    print_hex_line("X30:  ", frame->regs[30]);
}

// Prints frame then halts CPU for fatal exceptions.
static void __attribute__((noreturn)) exception_halt(const char *reason, const struct trap_frame *frame) {
    irq_disable();
    uart_puts("\nFatal exception\n");
    uart_puts("reason: ");
    uart_puts(reason);
    uart_puts("\n");
    trap_frame_dump(frame);

    while (1) {
        asm volatile("wfe");
    }
}

static struct trap_frame *handle_user_page_fault(struct trap_frame *frame, const char *reason);

static struct trap_frame *handle_sync_exception(struct trap_frame *frame) {
    uint64_t ec = (frame->esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    uint64_t iss = frame->esr & ESR_ISS_MASK;

    switch (ec) {
    case ESR_EC_BRK64:
        if ((iss & ESR_BRK_COMMENT_MASK) == BRK_SELFTEST) {
            uart_puts("BRK exception self-test passed\n");
            frame->elr += 4;
            return frame;
        }

        exception_halt("brk exception", frame);

    case ESR_EC_SVC64:
        if (frame->type != EXC_SYNC_LOWER_A64) {
            exception_halt("svc64 syscall invalid frame->type: not EXC_SYNC_LOWER_A64", frame);
        }

        /*
         * AArch64 SVC exceptions save the normal return address in ELR_EL1,
         * which is already the instruction after the SVC. Unlike BRK above,
         * do not manually advance ELR here.
         */
        irq_enable();
        struct trap_frame *next_frame = syscall_dispatch(frame);
        irq_disable();
        return next_frame;
        
    case ESR_EC_DABT_LOWER:
        return handle_user_page_fault(frame, "user data abort");

    case ESR_EC_IABT_LOWER:
        return handle_user_page_fault(frame, "user instruction abort");

    case ESR_EC_DABT_CURRENT:
        exception_halt("kernel data abort", frame);

    case ESR_EC_IABT_CURRENT:
        exception_halt("kernel instruction abort", frame);

    case ESR_EC_SYSREG_TRAP:
        exception_halt("system register access trap", frame);

    case ESR_EC_PC_ALIGN:
        exception_halt("pc alignment fault", frame);

    case ESR_EC_SP_ALIGN:
        exception_halt("sp alignment fault", frame);

    default:
        exception_halt(exception_class_name(ec), frame);
    }
}

static struct trap_frame *handle_user_page_fault(struct trap_frame *frame, const char *reason) {
    pcb_t *proc = get_curr_process();

    uart_puts("\nUser process fault\n");
    uart_puts("reason: ");
    uart_puts(reason);
    uart_puts("\n");
    uart_puts("pid: ");
    uart_puthex(proc != NULL ? (uint64_t)proc->pid : UINT64_C(0xffffffffffffffff));
    uart_puts("\nFAR: ");
    uart_puthex(frame->far);
    uart_puts("\nESR: ");
    uart_puthex(frame->esr);
    uart_puts("\n");

    if (proc == NULL) {
        exception_halt("user page fault without current process", frame);
    }

    proc->state = PROC_ZOMBIE_STATE;
    proc->exit_code = -1;
    schedule_yield();
    return frame;
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
    asm volatile("msr daifclr, #3" ::: "memory");
}

void irq_disable(void) {
    asm volatile("msr daifset, #3" ::: "memory");
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
    if (is_irq_type(frame->type)) {
        frame = irq_handle_exception(frame);
        run_scheduler_if_needed();
        return frame;
    }

    if (is_fiq_type(frame->type)) {
        return irq_handle_exception(frame);
    }

    if (is_serror_type(frame->type)) {
        exception_halt("serror exception", frame);
    }

    if (is_sync_type(frame->type)) {
        return handle_sync_exception(frame);
    }

    exception_halt("unknown exception vector", frame);
}
