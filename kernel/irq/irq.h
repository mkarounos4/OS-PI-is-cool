#pragma once

#include <stdint.h>

#include "traps/traps.h"

typedef void (*irq_handler_t)(unsigned intid, struct trap_frame *frame, void *ctx);

// Initialize GIC
void irq_init(void);

// Attach a c handeler for the given interrupt ID.
int irq_register(unsigned intid, irq_handler_t handler, void *ctx);

// Enable one GIC interrupt
void irq_enable_line(unsigned intid);

// Disable one GIC interrupt
void irq_disable_line(unsigned intid);

// Calls registered handler for the given trap frame if it's an IRQ exception. Called by trap exception dispatch.
struct trap_frame *irq_handle_exception(struct trap_frame *frame);

// Returns current IRQ nesting depth. 0 if not currently handling an IRQ, 1 if handling an IRQ but not currently in an IRQ handler, 2+ if currently in an IRQ handler and handling another IRQ.
unsigned irq_get_depth(void);

// Reads GIC identity registers
void irq_get_controller_info(uint32_t *typer, uint32_t *iidr);
