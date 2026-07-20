# Hardware and Kernel Architecture

This document describes the core hardware-facing architecture of the OS:
bare-metal boot, exception-level setup, MMU handoff, traps, interrupts, timer
events, syscall entry, fatal exception policy, and the initialization order that
brings the rest of the kernel online.

This is not just an index for other documentation. Filesystem, memory,
userspace, driver, process, and API documents describe their own areas in more
detail, but the hardware and CPU-control path is documented here.

## List of Features

- [Bare-metal boot path](#bare-metal-boot-path)
- [Exception level transition](#exception-level-transition)
- [Early MMU handoff](#early-mmu-handoff)
- [Kernel linker script and image layout](#kernel-linker-script-and-image-layout)
- [Kernel initialization order](#kernel-initialization-order)
- [Platform split](#platform-split)
- [MMIO and device memory](#mmio-and-device-memory)
- [Exception vector table](#exception-vector-table)
- [Trap frame layout](#trap-frame-layout)
- [Exception dispatch](#exception-dispatch)
- [Synchronous exceptions](#synchronous-exceptions)
- [Data and instruction aborts](#data-and-instruction-aborts)
- [IRQ controller architecture](#irq-controller-architecture)
- [IRQ dispatch flow](#irq-dispatch-flow)
- [Timer architecture](#timer-architecture)
- [Scheduler handoff from interrupts](#scheduler-handoff-from-interrupts)
- [System call boundary](#system-call-boundary)
- [Fatal exception policy](#fatal-exception-policy)
- [Subsystem integration](#subsystem-integration)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)

## System Structure

```
+--------------------------------------------------+
|                  EL0 userspace                   |
|      init, shell, commands, user libraries        |
+--------------------------------------------------+
|                 syscall ABI                      |
|          svc #0, x8 number, x0-x5 args           |
+--------------------------------------------------+
|                    EL1 kernel                    |
| scheduler, VM, FS, VFS, signals, TTY, drivers    |
+--------------------------------------------------+
|        traps, IRQ dispatch, timer, MMU state      |
+--------------------------------------------------+
|        UART, framebuffer, SDHCI, GIC/local IRQ    |
+--------------------------------------------------+
|             Raspberry Pi 5 / QEMU hardware        |
+--------------------------------------------------+
```

The key architectural rule is that low-level CPU and hardware details are
concentrated in a few places:

- `kernel/boot.S` owns the initial core entry and EL transition.
- `kernel/memory/mmu.S` and `kernel/memory/mmu.c` own early MMU enablement.
- `kernel/traps/vectors.S` owns register save/restore for exceptions.
- `kernel/traps/traps.c` owns exception classification and trap dispatch.
- `kernel/irq/irq.c` owns interrupt-controller setup and IRQ routing.
- `kernel/timer/timer.c` owns ARM generic timer events.
- platform driver files own hardware-specific UART/framebuffer/storage details.

# Detailed Architecture and Decisions

## Bare-metal Boot Path

The first kernel code runs in `kernel/boot.S` at `_start`. At this point there
is no C runtime, heap, scheduler, filesystem, or virtual memory environment
that higher-level code can rely on.

The boot assembly performs the minimum setup needed to enter C safely:

1. Read `mpidr_el1` and keep only core 0 running.
2. Park secondary cores in a `wfe` loop.
3. Load the physical boot stack from `_stack_top_phys`.
4. Clear frame-pointer and link-register state.
5. Detect the current exception level.
6. If already in EL1, continue to BSS clearing.
7. If in EL2, configure the EL2-to-EL1 return state and execute `eret`.
8. Clear the kernel BSS range from `__bss_start_phys` to `__bss_end_phys`.
9. Call `initialize_vm`.

Secondary cores are deliberately not brought online. The rest of the kernel is
written around a single-core execution model, so parking the other cores avoids
concurrency problems before locks, per-core state, and inter-processor
interrupts exist.

## Exception Level Transition

The kernel is designed to run in EL1. Firmware may enter the image at EL1 or
EL2, so `boot.S` handles both cases.

When entered at EL2, boot code:

- sets `sp_el1` to the boot stack.
- writes `HCR_EL2` with the AArch64 execution bit for lower EL.
- enables EL1 access to the physical counter/timer through `CNTHCTL_EL2`.
- clears `CNTVOFF_EL2`.
- sets `ELR_EL2` to `_enter_el1`.
- writes `SPSR_EL2` for the target EL1 state.
- executes `eret`.

After the `eret`, `_enter_el1` reloads the EL1 stack and continues through the
same BSS-clearing path as an image that started directly in EL1.

This keeps all later kernel code independent of the firmware's initial
exception level. `kernel_main` can assume it is executing as the EL1 kernel.

## Early MMU Handoff

`initialize_vm` builds temporary boot page tables and then calls
`initialize_mmu`. These early tables are intentionally broad. They map normal
RAM and the device regions needed while the kernel is still transitioning into
its final address layout.

The early MMU setup maps:

- normal memory for early kernel execution.
- QEMU local interrupt-controller space.
- Raspberry Pi PCIe/RP1-related regions.
- Raspberry Pi GIC regions.
- RP1 peripheral and MSI-X regions.

`initialize_mmu` programs:

- `MAIR_EL1` for normal/cacheable and device memory attributes.
- `TCR_EL1` for 48-bit TTBR0/TTBR1 virtual address spaces, 4KB granules, inner
  shareable write-back cache policy, and 40-bit physical addresses.
- `TTBR0_EL1` and `TTBR1_EL1` with the early page-table roots.
- `SCTLR_EL1.M` to enable address translation.

The final step happens in `kernel/memory/mmu.S`: after enabling translation,
the assembly reloads the virtual kernel stack from `_stack_top`, clears `x29`
and `x30`, and branches to `kernel_main`.

Later in `kernel_main`, `install_kernel_page_table()` replaces the early
`TTBR1_EL1` mapping with the final kernel page table. The final table maps
kernel text read/execute, rodata read-only, embedded user images read-only,
kernel data/heap/page-pool memory read/write, and device memory with device
attributes.

## Kernel Linker Script and Image Layout

The root `linker.ld` controls both where the boot image is loaded physically and
where the kernel expects to run virtually after the MMU is enabled.

The important constants are:

- `KERNEL_PHYS_BASE = 0x80000`, the physical load base used by the boot path.
- `KERNEL_VA_BASE = 0xffff000000000000`, the high-half virtual base used by the
  kernel after translation is enabled.
- `ENTRY(_start)`, which makes the assembly `_start` label the image entry.

The linker script uses separate program headers for boot code, boot data, kernel
text, and kernel data:

| Program header | Flags | Purpose |
|---|---:|---|
| `boot` | `5` | Early executable code that runs before the full virtual layout. |
| `bootdata` | `6` | Early boot page tables and other writable boot-only data. |
| `text` | `5` | Kernel executable/read-only content. |
| `data` | `6` | Kernel writable data, BSS, and stack reservation. |

The image starts at the physical load base. `.text.boot` is placed first and
kept explicitly so `_start`, early boot helpers, and `initialize_mmu_asm` cannot
be discarded. `.boot_pgtables` is marked `NOLOAD` and aligned to 4096 bytes so
the early MMU has page-table storage without requiring initialized file data.

After the boot region, the linker moves the location counter into the kernel's
high-half virtual address range:

```
. = KERNEL_VA_BASE + __boot_end_phys;
```

From that point on, kernel sections have high virtual addresses, but their load
addresses are still physical image offsets through `AT(ADDR(section) -
KERNEL_VA_BASE)`. This is why the script exports both virtual and physical
symbols such as `__text_start` and `__text_start_phys`. Early boot code needs
physical symbols before translation is stable; normal kernel code uses virtual
symbols after the MMU handoff.

The kernel image sections are ordered as:

1. `.text.boot`: physical early entry and MMU assembly.
2. `.boot_pgtables`: early page tables, `NOLOAD`, page aligned.
3. `.text`: normal kernel code.
4. `.rodata`: read-only kernel data.
5. `.user_image`: generated embedded user image data.
6. `.data`: writable initialized kernel data.
7. `.bss`: zeroed kernel data, `NOLOAD`.
8. `.stack`: reserved kernel boot stack, `NOLOAD`.
9. page pool: free physical memory starts at `__kernel_page_pool_start`.

The `.user_image` section is intentionally page-aligned and wrapped by
`__user_image_start` and `__user_image_end`. The build generates an object that
places raw user-image bytes into this section with `.incbin`. The final kernel
page table maps this section read-only, so the kernel can retain boot/user image
data without mixing it with writable kernel state.

The script also defines `__RAM_end_phys = 0x40000000` and derives `__RAM_end`
from the high-half base. The page allocator uses the region after
`__kernel_page_pool_start` up to `__RAM_end` as general page-pool memory.

This linker layout is the reason boot has two address vocabularies:

- physical addresses before and during the first MMU enable.
- high-half virtual addresses after `initialize_mmu_asm` branches to
  `kernel_main`.

Keeping both symbol families in the linker script makes the transition explicit
and avoids scattering magic offsets through boot, MMU, and page-table code.

## Kernel Initialization Order

`kernel_main` brings the system up in dependency order:

1. `uart_init()` enables early serial output.
2. `gui_framebuffer_init()` initializes framebuffer output.
3. `fan_init()` initializes Raspberry Pi fan support when available.
4. `exceptions_init()` installs the exception vector table.
5. `irq_init()` initializes the interrupt controller path.
6. `uart_irq_init()` registers and enables UART RX interrupts.
7. `timer_init()` registers and enables the ARM generic timer interrupt.
8. `irq_enable()` clears interrupt mask bits in `DAIF`.
9. `install_kernel_page_table()` installs the final kernel page table.
10. `kmem_init()` initializes the kernel heap.
11. `init_tty_gui()` initializes GUI terminal renderer state.
12. `pt_init()` initializes physical-page metadata.
13. `block_init()` initializes persistent storage.
14. `mount()` mounts the filesystem, or `mkfs()` creates it and then mounts;
    this also registers `/proc` and `/dev` virtual root mounts.
15. Character-device registry and char drivers are initialized.
16. `/dev/uart0` and the initial `tty0`/`ttygui0` terminal pair are populated
    as devfs nodes.
17. `initialize_signals()` initializes signal defaults.
18. `scheduler_init()` creates scheduler/process/thread state.
19. `scheduler_start()` switches away from boot context and begins scheduling.

This order is intentionally strict. UART must work early for diagnostics.
Exception vectors must exist before IRQs are enabled. The heap and page
allocator must exist before process creation. The filesystem must be mounted
before `/bin/init`, `/bin/shell`, and root virtual mounts can be used. The scheduler is
started only after the kernel has enough infrastructure to run userspace.

## Platform Split

The build selects platform-specific files while keeping common kernel code
behind stable interfaces.

| Area | QEMU path | Raspberry Pi 5 path | Common interface |
|---|---|---|---|
| UART | emulated UART registers | RP1 UART0 through Pi 5 MMIO setup | `uart_init`, `uart_raw_putc`, UART IRQ hook |
| IRQ | Raspberry Pi 3 local IRQ and BCM pending registers | GIC distributor/CPU interface plus RP1 interrupt bridge | `irq_register`, `irq_enable_line`, `irq_handle_exception` |
| Timer | ARM generic physical timer | ARM generic physical timer | `timer_init`, software timer API |
| Storage | block path through SDHCI abstraction | SDHCI-backed SD card | `block_read`, `block_write`, geometry helpers |
| Output | UART and framebuffer depending on build | UART and framebuffer | TTY/TTYGUI/backend fops |

The rest of the kernel should not care which platform supplied a device. For
example, the filesystem calls `block_read`, not SDHCI internals. The syscall
path calls `write` on a file descriptor, not UART or framebuffer directly.

## MMIO and Device Memory

Device drivers access hardware through memory-mapped registers. Those register
ranges must be mapped with device memory attributes, not normal cacheable memory
attributes. The page-table setup therefore has explicit device mappings for the
interrupt-controller and peripheral blocks used by the supported platforms.

The architecture separates these concerns:

- low-level drivers know register offsets and hardware-specific protocols.
- page-table setup knows which physical regions must be mapped as devices.
- higher-level subsystems call driver APIs and do not perform raw MMIO.

This matters for correctness. Normal memory can be cached and reordered more
aggressively. Device memory needs stronger ordering so register writes reach the
hardware in the intended order. The code uses barriers around important MMU and
interrupt-controller transitions for the same reason.

## Exception Vector Table

AArch64 exception vectors are installed with `VBAR_EL1` in `exceptions_init`.
The vector table lives in `kernel/traps/vectors.S` and is aligned to the
architecture-required vector-table boundary.

There are 16 entries:

| Vector group | Synchronous | IRQ | FIQ | SError |
|---|---:|---:|---:|---:|
| Current EL using SP0 | 0 | 1 | 2 | 3 |
| Current EL using SPx | 4 | 5 | 6 | 7 |
| Lower EL AArch64 | 8 | 9 | 10 | 11 |
| Lower EL AArch32 | 12 | 13 | 14 | 15 |

Each vector entry:

1. Allocates a fixed-size trap frame on the current stack.
2. Saves `x0` and `x1`.
3. Places the vector type number in `x0`.
4. Branches to the shared `exception_entry`.

`exception_entry` saves the rest of the general registers, captures system
diagnostic registers, calls the C dispatcher, restores the selected frame, and
returns with `eret`.

## Trap Frame Layout

The C and assembly sides share one exact `struct trap_frame` layout. This is
enforced with static assertions in `traps.c`.

| Offset | Field | Meaning |
|---:|---|---|
| `0` | `regs[31]` | Saved general-purpose registers `x0` through `x30`. |
| `248` | `sp` | Saved stack pointer for the interrupted context. |
| `256` | `elr` | `ELR_EL1`, the PC that `eret` returns to. |
| `264` | `spsr` | `SPSR_EL1`, saved PSTATE for the interrupted context. |
| `272` | `esr` | `ESR_EL1`, exception syndrome information. |
| `280` | `far` | `FAR_EL1`, fault address for aborts. |
| `288` | `type` | Vector type from the 16-entry exception table. |
| `296` | `intid` | Interrupt id filled by IRQ dispatch. |

For exceptions from lower EL, the vector code stores `SP_EL0` in the frame. For
current-EL exceptions, it stores the pre-exception kernel stack pointer by
adding the trap-frame size back to `sp`.

The restore path writes `ELR_EL1` and `SPSR_EL1`, restores `SP_EL0` when
returning to EL0, reloads all general registers, releases the trap-frame stack
space, and executes `eret`.

This design lets C code inspect or even replace the frame that will be resumed.
The syscall `exec` path uses that ability to return a different trap frame when
the process image is replaced.

## Exception Dispatch

`exception_dispatch(frame)` classifies the vector type first:

- IRQ vectors call `irq_handle_exception(frame)`, then
  `run_scheduler_if_needed()`.
- FIQ vectors currently reuse the IRQ dispatcher.
- SError vectors are fatal.
- Synchronous vectors call `handle_sync_exception(frame)`.
- Unknown vector types are fatal.

This keeps the assembly entry path generic. Assembly only saves CPU state and
calls C. C decides whether the event is a syscall, page fault, hardware
interrupt, debug break, or fatal exception.

## Synchronous Exceptions

Synchronous exceptions are decoded from `ESR_EL1`. The main fields used are:

| Field | Bits | Meaning |
|---|---:|---|
| `EC` | `31:26` | Exception class. |
| `ISS` | `23:0` | Instruction-specific syndrome. |
| `FSC` | `5:0` of ISS | Fault status code for aborts. |
| `WnR` | bit `6` of ISS | Data abort was write when set, read when clear. |

Handled exception classes include:

| EC | Meaning | Kernel behavior |
|---:|---|---|
| `0x15` | `SVC64` | Dispatch syscall if it came from lower AArch64 EL. |
| `0x18` | System register trap | Fatal exception. |
| `0x20` | Instruction abort from lower EL | MMU instruction-abort handler. |
| `0x21` | Instruction abort from current EL | MMU instruction-abort handler. |
| `0x22` | PC alignment fault | Fatal exception. |
| `0x24` | Data abort from lower EL | MMU data-abort handler. |
| `0x25` | Data abort from current EL | MMU data-abort handler. |
| `0x26` | SP alignment fault | Fatal exception. |
| `0x3c` | `BRK64` | Self-test break `0x42` advances `ELR`; other breaks are fatal. |

For `SVC64`, the kernel requires `EXC_SYNC_LOWER_A64`. A syscall from any other
vector type is treated as a fatal invalid syscall frame. The code does not
manually advance `ELR_EL1` for SVC because AArch64 already stores the return PC
as the instruction after `svc`.

For `BRK64`, the self-test comment value `0x42` is special-cased. That path
prints a success message and advances `ELR` by 4 bytes so execution resumes
after the `brk` instruction. Other `BRK64` exceptions are fatal.

## Data and Instruction Aborts

Data and instruction aborts are normal control flow for parts of the memory
system.

Instruction aborts are used for demand-loading executable pages:

1. Decode `FSC` from `ESR_EL1`.
2. For translation faults, get the current process.
3. Ask `load_segment_page_for_fault` to load the page for the faulting virtual
   address as an instruction fetch.
4. If handled, invalidate stage-1 TLBs and return to the faulting context.
5. If the segment exists but is not executable, report a permission fatal
   exception.
6. If the address is not part of a known segment, report an unmapped-address
   fatal exception.

Data aborts use the same fault-status idea, but they additionally handle user
heap growth, user stack growth, lazy segment data pages, and copy-on-write
permission faults. If a write hits a shared read-only COW page, the fault
handler can allocate a private page, copy data, update page-table permissions,
invalidate TLBs, and resume the process.

Faults that cannot be recovered become fatal exceptions. Examples include
address-size faults, invalid external aborts, access-flag faults that are not
supported by the current policy, unsupported atomic update faults, and accesses
outside the process's valid virtual ranges.

## IRQ Controller Architecture

The IRQ subsystem exposes a platform-neutral interface:

- `irq_init()`
- `irq_register(intid, handler, ctx)`
- `irq_enable_line(intid)`
- `irq_disable_line(intid)`
- `irq_set_edge_triggered(intid)`
- `irq_handle_exception(frame)`
- `irq_get_depth()`

Internally, the implementation differs by platform.

### QEMU IRQ Path

QEMU uses Raspberry Pi 3-style local interrupt state and BCM interrupt pending
registers. During `irq_init`, the kernel:

- disables CPU IRQ exceptions.
- clears the core timer interrupt control register.
- clears GPU interrupt routing.
- disables the UART pending line.
- executes barriers before returning.

`irq_enable_line` maps known interrupt ids to platform registers:

- the ARM generic timer enables local CNTP IRQ bits.
- UART enables the BCM UART0 pending bit.

When an IRQ arrives, `irq_handle_exception` reads the local core IRQ source. If
the GPU IRQ bit is set, it checks the BCM pending register for UART0. If the
timer bits are set, it selects the ARM generic timer interrupt id.

### Raspberry Pi 5 IRQ Path

Raspberry Pi 5 uses the GIC path. During `irq_init`, the kernel:

- disables CPU IRQ exceptions.
- disables the GIC distributor and CPU interface.
- reads `GICD_TYPER` and clamps the supported interrupt count.
- disables all interrupt lines.
- clears pending and active state.
- clears the interrupt group registers for the current simple setup.
- sets default priorities.
- targets shared peripheral interrupts at CPU 0.
- configures level-triggered defaults.
- enables the CPU interface and distributor.

Drivers can then register handlers and enable individual lines. UART on Pi 5
also performs RP1 interrupt-bridge/MSI-X setup before enabling RX interrupts.

## IRQ Dispatch Flow

The IRQ dispatch path is:

```
hardware interrupt
  -> vector entry type IRQ
  -> exception_entry saves trap_frame
  -> exception_dispatch
  -> irq_handle_exception
  -> registered driver handler
  -> optional scheduler handoff
  -> restore trap_frame
  -> eret
```

`irq_handle_exception` fills `frame->intid`, ignores spurious/invalid interrupt
ids, increments per-interrupt counts, increments `irq_depth`, and calls the
registered handler.

Handlers have this type:

```c
typedef struct trap_frame *(*irq_handler_t)(unsigned intid,
                                           struct trap_frame *frame,
                                           void *ctx);
```

The handler may return the same trap frame or a different frame. Returning a
different frame gives interrupt-time code a way to redirect the restored
context, although most current handlers return the original frame.

If no handler is registered for a valid interrupt id, the IRQ layer disables
that line. On the GIC path, `GICC_EOIR` is written after dispatch so the
interrupt controller knows the interrupt has completed. `irq_depth` is then
decremented.

The IRQ layer also keeps interrupt counts for `/proc` reporting.

## Timer Architecture

The timer subsystem is built on the ARM generic physical timer. `timer_init`
disables the timer, reads `CNTFRQ_EL0`, clears the software timer table,
registers the timer IRQ handler, and enables the timer interrupt line.

The kernel tracks software timers in a fixed-size table. Each active timer has:

- a deadline in counter ticks.
- a callback function.
- a callback context pointer.

When timers are scheduled, the timer code arms the hardware for the nearest
deadline. If there are no active software timers, it disables the hardware
timer. If the nearest deadline is too far for the timer compare register, it
caps the hardware delay and rearms later.

The timer IRQ handler:

1. Disables the hardware timer.
2. Increments the timer tick counter.
3. Finds expired software timers.
4. Clears expired timer slots.
5. Runs expired callbacks.
6. Rearms the hardware timer for the next deadline.

Callbacks run from interrupt context, so they should do small state transitions
and wakeups rather than long blocking work.

## Scheduler Handoff From Interrupts

The scheduler is connected to the timer through a deferred handoff. The timer
callback used for preemption sets scheduler state indicating that a scheduling
decision is needed. After IRQ dispatch returns, `exception_dispatch` calls
`run_scheduler_if_needed()`.

This keeps context switching out of the middle of the low-level IRQ controller
logic. The interrupt handler marks that scheduling should happen; the exception
dispatcher performs the handoff after the interrupt line has been serviced.

The scheduler itself stores runnable threads in three priority queues. It uses
small counters to rotate between priorities after each priority has run a
configured number of times. If no runnable thread exists, the scheduler switches
to an idle context that waits with `wfe`.

Blocking kernel operations should block the current thread instead of spinning.
TTY input, sleeps, waitpid, signal waits, synchronization primitives, and other
events wake threads by moving them back to a runnable state.

## System Call Boundary

Userspace enters the kernel with `svc #0`. The userspace wrapper ABI is:

- `x8`: syscall number.
- `x0` through `x5`: up to six arguments.
- return value: `x0`.

The inline syscall wrapper marks caller-saved registers and memory as clobbered,
then returns the value placed in `x0` by the kernel.

The kernel syscall path is:

```
user wrapper
  -> svc #0
  -> lower-EL synchronous exception
  -> exception_entry
  -> handle_sync_exception
  -> syscall_dispatch
  -> subsystem implementation
  -> frame->regs[0] = return value
  -> eret to EL0
```

`syscall_dispatch` increments syscall counters for `/proc`, switches on the
number in `x8`, calls the owning subsystem, and writes the result into
`frame->regs[0]`.

Some syscalls affect control flow:

- `yield` sets the return value and calls `schedule_yield`.
- `block_until_event` returns a trap frame from the blocking path.
- `exec` may return a replacement trap frame for the new process image.
- filesystem syscalls convert internal filesystem errors into userspace errno
  values.

This keeps the trap handler narrow. It knows how to identify SVC exceptions,
but it does not implement filesystem, process, signal, or TTY policy.

## Fatal Exception Policy

Fatal exceptions are handled by `handle_fatal_exception`. The handler prints a
reason, prints the current thread id when one exists, and dumps the trap frame
if available.

The dump includes:

- vector type.
- decoded exception class.
- ISS.
- fault status for aborts.
- read/write direction for data aborts.
- raw `ESR`, `ELR`, `FAR`, `SPSR`, and `SP`.
- selected registers such as `x0`, `x1`, `x2`, `x8`, and `x30`.

If there is a current thread, the kernel terminates that thread. If there is no
current thread, the kernel disables IRQs and halts in a `wfe` loop. This
distinction is important: once the scheduler is running, a bad user process
should not necessarily bring down the whole kernel, but a fatal exception before
thread context exists cannot be recovered safely.

## Subsystem Integration

The hardware architecture connects to the rest of the OS through a few stable
contracts:

- traps provide a saved `trap_frame` for syscalls, page faults, and interrupts.
- IRQ dispatch provides registered interrupt callbacks by interrupt id.
- the timer provides delayed callbacks and scheduler preemption.
- MMU fault handlers provide lazy allocation, demand paging, and COW recovery.
- block devices provide persistent storage to the filesystem.
- character devices expose hardware and terminal backends through VFS fops.
- signals and process groups integrate terminal-generated events with the
  scheduler.

The shell is a good end-to-end example:

```
UART interrupt
  -> UART char device RX buffer
  -> TTY canonical input
  -> shell read from /dev/tty0
  -> shell fork/exec through syscalls
  -> ELF loader maps a user program
  -> page faults demand-load executable pages
  -> timer IRQ preempts and resumes runnable threads
```

No single layer owns that whole flow. The architecture works because each layer
has a narrow interface and the trap frame provides one common way to return to
the interrupted context.

## Design Tradeoffs and Limits

The architecture favors a small, understandable kernel over a production-style
hardware abstraction layer.

Current limits include:

- single-core execution; secondary cores are parked at boot.
- build-time platform selection instead of runtime hardware discovery.
- fixed interrupt handler table.
- no full device tree probing layer.
- no dynamic kernel modules.
- no arbitrary block-device registry.
- IRQ callbacks run in interrupt context and should stay small.
- fatal kernel exceptions before scheduler startup halt the system.

These constraints keep the hardware path debuggable while still covering the
core OS mechanisms: EL transition, MMU enablement, exception vectors, trap-frame
restore, syscalls, page faults, interrupt dispatch, timer events, scheduling,
drivers, and userspace execution.
