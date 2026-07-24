# Device Drivers Architecture

This document describes the device-driver architecture used by the kernel. The
driver layer has two jobs:

- Talk to platform hardware such as UART, SDHCI storage, framebuffer, timers,
  interrupts, and the Raspberry Pi fan.
- Expose hardware and pseudo-devices through stable kernel interfaces such as
  block I/O, character-device file operations, and TTY backends.

The kernel deliberately keeps hardware access behind small boundaries. Most
subsystems should not write MMIO registers directly. They should call stable
wrappers such as `block_read`, `block_write`, `char_device_read`,
`char_device_write`, `tty_write`, or an inode's `file_operations`.

## List of Features

- [Block-device abstraction](#block-devices)
- [SDHCI-backed storage](#sdhci-storage)
- [Platform UART driver](#platform-uart-driver)
- [Character-device registry](#character-device-registry)
- [Device nodes and devfs creation](#device-nodes-and-devfs-creation)
- [UART character device](#uart-character-device)
- [TTY frontend](#tty-frontend)
- [TTY input processing](#tty-input-processing)
- [TTY output routing](#tty-output-routing)
- [TTYGUI framebuffer backend](#ttygui-framebuffer-backend)
- [Terminal lifecycle](#terminal-lifecycle)
- [Pipes](#pipes)
- [Fan driver](#fan-driver)
- [Driver initialization order](#driver-initialization-order)
- [Error handling and limitations](#error-handling-and-limitations)

## System Structure

```
+--------------------------------------------------+
|          Userspace syscalls and shell I/O         |
+--------------------------------------------------+
|        Filesystem paths, /dev nodes, OFT          |
+--------------------------------------------------+
|          VFS file_operations dispatch             |
+--------------------------------------------------+
|       Character-device registry and fops          |
+--------------------------------------------------+
|        TTY frontend, UART backend, TTYGUI         |
+--------------------------------------------------+
|   UART MMIO, framebuffer, SDHCI, fan, IRQ, timer  |
+--------------------------------------------------+
|              Raspberry Pi / QEMU hardware         |
+--------------------------------------------------+
```

There are two different kinds of driver interfaces in the current kernel:

| Interface | Used by | Purpose |
|---|---|---|
| Block device API | Filesystem disk layer | Reads and writes fixed-size logical blocks. |
| Character device API | `/dev` inodes and kernel drivers | Exposes byte streams through `open`, `close`, `read`, and `write`. |
| Direct platform API | Early boot and low-level drivers | Initializes MMIO devices before the filesystem and `/dev` exist. |

# Detailed Architecture and Decisions

## Block Devices

The block layer is the storage boundary used by the filesystem. Its public API
is:

- `block_init()`
- `block_read(lba, count, buf)`
- `block_write(lba, count, buf)`
- `block_get_info()`
- `block_get_count()`
- `block_get_size()`

The current implementation delegates directly to the SDHCI backend:

```
filesystem disk layer
  -> block_read / block_write
  -> sdhci_block_read / sdhci_block_write
  -> SD card controller
```

The filesystem does not know about SDHCI registers, command descriptors, or
platform setup. It only sees logical block addresses, a sector count, and a
sector size. This is what lets `mount` and `mkfs` validate the superblock block
size against `block_get_size()` and choose a filesystem region using
`block_get_count()`.

There is currently no general block-device registry. That is intentional for
this stage of the OS: there is one persistent storage backend, so a global
registry would add machinery without changing behavior. If the OS later gains
multiple disks, loop devices, or ramdisks, the existing block API is the natural
place to introduce a registry.

## SDHCI Storage

The SDHCI driver is the persistent storage driver used on Raspberry Pi
hardware. It initializes the card/controller path and performs sector reads and
writes. Higher layers treat SDHCI sectors as filesystem blocks.

The design boundary is important:

- SDHCI owns card/controller setup and block transfer.
- `kernel/disk/block.c` exposes the simple block API.
- `kernel/fs/disk.c` owns filesystem layout, mount validation, and partition
  region selection.

Partition handling does not live in the SDHCI driver. On Raspberry Pi builds,
the filesystem code reads the MBR/GPT layout and chooses a filesystem region
after the original boot/data partition. On QEMU builds, the filesystem uses the
whole block device. Keeping this logic above SDHCI keeps the storage driver
usable as a plain block transport.

## Platform UART Driver

UART has a platform layer and a character-device layer.

The platform layer is split by target:

- `uart_qemu.c` programs the emulated PL011-style UART used by QEMU.
- `uart_rpi.c` programs the Raspberry Pi 5 UART path through RP1/MMIO setup.

This split exists because the hardware setup is meaningfully different between
QEMU/RPi3-style emulation and the Raspberry Pi 5 path. Both files still expose
the same higher-level functions:

- `uart_init()`
- `uart_irq_init()`
- `uart_raw_putc()`
- `uart_putc()`
- `uart_puts()`
- `uart_rx_interrupts_enable()`
- `uart_rx_interrupts_disable()`
- `uart_rx_interrupt_hook()`

Early boot output uses the platform UART directly because the filesystem,
scheduler, and `/dev` nodes may not exist yet. Once the kernel has mounted the
filesystem and registered char drivers, normal terminal I/O can flow through
`/dev/uart0` and the TTY layer.

### UART Interrupt Flow

Input is interrupt-driven:

```
UART RX interrupt
  -> platform IRQ handler
  -> uart_rx_interrupt_hook()
  -> uart_char_device_receive()
  -> /dev/uart0 RX ring
  -> tty_receive_input_from_device()
  -> active TTY input line
```

The IRQ handler disables UART RX interrupts while it drains hardware state,
acknowledges the interrupt, calls the receive hook, and then re-enables RX
interrupts. This keeps input delivery serialized around the driver's RX buffer.

## Character Device Registry

Character devices are exposed through devfs as virtual inodes with type
`CHAR_DRIVER_TYPE`. Each device inode stores an `i_rdev` pair:

```c
struct dev_st {
    uint16_t major;
    uint16_t minor;
};
```

The major number selects the driver. The minor number selects an instance of
that driver.

Current major assignments are:

| Major | Driver | Device nodes | Purpose |
|---:|---|---|---|
| 0 | `tty` | `/dev/ttyN` | User-facing terminal frontend. |
| 1 | `uart` | `/dev/uart0` | UART byte-stream backend and direct UART access. |
| 2 | `ttygui` | `/dev/ttyguiN` | Framebuffer terminal backend. |

The registry is a fixed array of 16 `struct char_driver *` entries. A
`char_driver` contains:

- `name`: base name used when creating `/dev/<name><minor>`.
- `major`: registry slot.
- `fops`: file operation table used by VFS/OFT dispatch.
- `driver_data`: optional pointer to driver-owned state.

`initialize_char_device_registry()` clears the registry during boot.
`register_char_driver(driver)` validates the driver, rejects majors outside the
registry, rejects duplicate majors, and stores the driver pointer. The registry
does not copy the driver object, so drivers register static driver descriptors.

This gives the kernel a small but useful driver boundary:

```
open("/dev/tty0")
  -> inode metadata says CHAR_DRIVER_TYPE
  -> inode metadata contains i_rdev = { TTY_MAJOR, 0 }
  -> inode fops point at tty_fops
  -> tty_open / tty_read / tty_write handle the operation
```

For more information about char drivers with respect to the Filesystem, please check out [filesystem.md](filesystem.md).

## Device Nodes and devfs Creation

`/dev` is a root virtual filesystem registered by `devfs_init()` during
filesystem mount. It uses the same VFS root-mount machinery as `procfs`, with
the synthetic inode range rooted at `DEVFS_INO_BASE`.

`devfs_create_char_device(rdev)` creates or updates one in-memory devfs entry
for a major/minor pair. It does not create `/dev`, allocate disk inodes, or
write persistent dirents. Device nodes are regenerated from registered char
drivers and live devfs entries each boot.

The creation flow is:

1. Validate that `rdev.major` has a registered char driver.
2. Build the device name from the driver name and minor number, such as
   `tty0`, `uart0`, or `ttygui0`.
3. Reuse an existing devfs slot with the same major/minor or name, if one
   exists.
4. Otherwise allocate a free devfs node slot.
5. Mark the slot active and store the generated name and `i_rdev`.

Path lookup resolves `/dev` through the VFS root mount table. Lookup then
continues inside devfs, where names such as `tty0` are mapped to synthetic
inodes. Metadata for those inodes is generated on demand: type
`CHAR_DRIVER_TYPE`, permissions `0x7`, `i_rdev`, and fops from the registered
char driver.

This means device files do not survive as disk entries. Recreating a device
node is safe because the in-memory devfs slot is refreshed rather than creating
duplicate dirents.

The helpers `char_device_read(rdev, buffer, count)` and
`char_device_write(rdev, buffer, count)` let one kernel driver call another char
driver without going through a userspace file descriptor. They construct a small
temporary `oft_entry` and cached-inode wrapper containing the requested `i_rdev`,
then call the registered driver's fops.

## UART Character Device

`uart_device.c` registers the UART char driver:

- Driver name: `uart`
- Major: `UART_MAJOR`
- Device count: 1
- Node: `/dev/uart0`
- Internal buffer size: 4096 bytes for RX and 4096 bytes for TX

The UART char device owns one active `uart_device` instance. It contains:

- RX ring buffer.
- TX ring buffer.
- RX wait queue.
- open refcount.
- active flag.

Read behavior:

- `uart_dev_read` consumes bytes from the RX ring.
- If the RX ring is empty and no bytes have been read yet, the current thread is
  added to the RX wait queue and blocked interruptibly.
- When new hardware input arrives, `uart_char_device_receive` wakes waiting
  readers.

Write behavior:

- `uart_dev_write` queues bytes into the TX ring.
- Queued bytes are drained immediately to `uart_raw_putc`.
- `\n` is emitted as `\r\n` for terminal compatibility.

The UART char device is both directly usable as `/dev/uart0` and used as the
input backend for TTY devices.

## TTY Frontend

The TTY driver is the user-facing terminal frontend. User programs normally
open `/dev/ttyN`, not `/dev/uart0` or `/dev/ttyguiN`.

Each active TTY contains:

- `device_number`: its own `{ TTY_MAJOR, minor }`.
- `input_backend`: the char device that supplies input.
- `output_backend`: the char device that receives rendered output.
- RX/TX ring buffers.
- RX/TX wait queues.
- open refcount.
- foreground process group.
- canonical/raw mode.
- active flag.
- alternate-screen session state.
- per-terminal command input buffer.
- per-terminal command history stack.

TTYs are represented as char devices because the shell and userspace programs
should interact with terminals through normal file descriptors. The TTY fops
handle terminal semantics, while UART and TTYGUI provide concrete input/output
backends.

### Canonical and Raw Mode

`TTY_MODE_CANONICAL` is line-oriented. Input editing, control characters, and
history traversal happen in the TTY before bytes are made available to readers.
This is the normal shell mode.

`TTY_MODE_RAW` bypasses canonical line handling. Incoming bytes are placed into
the TTY RX stream directly. Full-screen programs and alternate-screen sessions
can use this mode when they need direct key input.

The mode is per TTY, not global. Switching modes for one terminal does not
change how other terminals process input.

## TTY Input Processing

Input is backend-driven. The TTY does not read the UART hardware directly. It
reads from whichever char device is configured as its input backend.

The current default is:

```
TTY input backend = { UART_MAJOR, 0 }
```

When UART input arrives, `uart_char_device_receive` stores bytes in `/dev/uart0`
and calls `tty_receive_input_from_device`. The TTY layer then reads those bytes
through `char_device_read` and routes each byte to the active TTY that is bound
to that backend.

Canonical input handles:

| Byte | Meaning |
|---:|---|
| `0x03` | `Ctrl-C`, sends `SIGINT` to the foreground process group. |
| `0x04` | `Ctrl-D`, commits EOF when the input line is empty. |
| `0x0A` | Switch to the previous visible terminal. |
| `0x0B` | Switch to the next visible terminal. |
| `0x0C` | `Ctrl-L`, commits a clear-screen request. |
| `0x0D` | Enter, commits the current line. |
| `0x14` | Request a new terminal tab. |
| `0x17` | Delete the current terminal tab. |
| `0x18` | Recall previous command-history entry. |
| `0x19` | Recall next command-history entry. |
| `0x1A` | `Ctrl-Z`, sends `SIGTSTP` to the foreground process group. |
| `0x1B` | Start escape-sequence parsing. |
| `0x7F` | Backspace. |

The TTY also recognizes common terminal escape arrows after `ESC [` or `ESC O`.
`A` and `B` map to command-history movement, while `C` and `D` move the input
cursor right and left. The dedicated GUI arrow bytes `0x18` and `0x19` are
handled before escape parsing so framebuffer keyboard events can traverse
history without emitting terminal escape text.

Command history is per TTY:

- Maximum depth is `TTY_COMMAND_HISTORY_DEPTH` entries.
- Each saved command is capped at `TTY_COMMAND_HISTORY_LINE_SIZE`.
- Empty lines are not saved.
- Repeating the newest command does not duplicate it.
- Up/down navigation keeps a scratch copy of the in-progress line so the user
  can return to it after browsing history.

## TTY Output Routing

TTY output is backend-driven in the same way as input. Each TTY stores an
`output_backend` device number.

Default output depends on the build:

- Normal framebuffer builds write `/dev/ttyN` output to `/dev/ttyguiN`.
- `UART_OUT` builds write `/dev/ttyN` output to `/dev/uart0`.

This is why the TTY layer does not hardcode framebuffer rendering or UART
printing. It sends bytes to a registered char backend with `char_device_write`.
The backend decides what writing means:

- UART emits bytes to the UART hardware.
- TTYGUI updates terminal cells and framebuffer pixels.

This split lets the same shell and process code run with a serial console, a
graphical terminal, or another future backend.

## TTYGUI Framebuffer Backend

The GUI terminal has two layers:

- `tty_gui.c` owns framebuffer rendering, terminal cell arrays, cursor state,
  tab visibility, and active-terminal redraws.
- `tty_gui_device.c` exposes the renderer as a char driver named `ttygui`.

The `ttygui` char driver uses:

- Major: `TTY_GUI_MAJOR`
- Device nodes: `/dev/ttyguiN`
- Device count: `MAX_TTY_DEVICES`
- Internal buffer size: 4096 bytes for RX and 4096 bytes for TX

Each active TTYGUI device contains:

- RX ring buffer.
- TX ring buffer.
- RX wait queue.
- open refcount.
- active flag.

TTYGUI writes are terminal-indexed by minor number. `tty_gui_dev_write`
extracts the minor from `i_rdev`, queues bytes into the backend TX ring, drains
them, and calls `tty_gui_write_char_for_tty(minor, ch)`. The renderer then
updates the matching terminal's cell buffer and redraws the active terminal when
needed.

The renderer handles:

- newline by advancing rows.
- backspace by moving the cursor backward.
- form feed by clearing the screen.
- tab by writing tab spacing on the active terminal.
- normal characters by updating the cell array and drawing the glyph.

TTYGUI devices are activated lazily. `tty_gui_char_driver_init()` only clears
the device table and registers the driver. It does not allocate all terminal
buffers. `tty_gui_char_device_activate(minor)` populates the `/dev/ttyguiN`
devfs node and allocates that backend's ring buffers when a terminal is
actually created.

## Terminal Lifecycle

Terminals are allocated lazily so boot does not allocate every possible TTY and
TTYGUI pair. The maximum number of TTYs is still fixed by `MAX_TTY_DEVICES`, but
only active terminals consume per-terminal heap and framebuffer cell storage.

Boot creates the first terminal:

```
tty_create()
  -> devfs_create_char_device({ TTY_MAJOR, 0 })
  -> tty_gui_create_terminal(0)
  -> tty_gui_char_device_activate(0)
  -> allocate struct tty_device for tty0
  -> create tty0 RX/TX rings and wait queues
```

Creating a new terminal tab follows the same path with the first free minor
number. In canonical input mode, byte `0x14` records a pending terminal-create
request and wakes the code waiting for a TTY request. The shell supervisor can
then call `tty_create()` and launch a shell on the returned minor.

Deleting a terminal tab is also explicit:

```
tty_delete(minor)
  -> reject invalid, inactive, invisible, or final visible terminal
  -> tty_gui_destroy_terminal(minor)
  -> tty_gui_char_device_deactivate(minor)
  -> destroy tty rings and wait queues
  -> free struct tty_device
```

The delete path wakes blocked TTY readers before freeing the device. It also
refuses to delete the last visible terminal, so there is always at least one
interactive terminal left.

This lifecycle keeps three pieces in sync:

- `/dev/ttyN`, the user-facing terminal frontend.
- `/dev/ttyguiN`, the framebuffer output backend.
- the GUI terminal cell/cursor state used for rendering the tab.

## Pipes

Pipes are not hardware drivers, but they use the same file-operation pattern as
character devices. A pipe endpoint behaves like an inode file with custom
read/write behavior and internal buffering.

The important similarity is that code above the filesystem still calls normal
`read` and `write`. The inode's fops decide what behavior to use.

Internally, each pipe has the following metadata:
- `num_readers`: number of threads with this pipe open in `read` mode
- `num_writers`: number of threads with this pipe open in `write`  mode
- `buffer`: a ring-buffer with max size `4096` storing pipe data
- `rx_wait_queue`: vec storing tids of all blocked readers waiting for input
- `tx_wait_queue`: vec storing tids of all blocked writers waiting for room in the buffer
This metadata is stored in the pipe's `inode` in the `Union` field `i_pipe`.

When writing, bytes are copied over into the `buffer`. Once the buffer is full, if there are more characters to write, this thread adds its `tid` to the `tx_wait_queue` and blocks until the `pipe` wakes it up. When it finishes writing, writes a `EOF`, or closes the file, it adds an `EOF` to the buffer and wakes up all tids in the `rx_wait_queue`.

When reading, bytes are consumed from the `buffer`. When the buffer is empty, this thread adds its `tid` to the `rx_wait_queue` and blocks until the `pipe` wakes it up, unless there are no readers, in which case it returns. It also returns if it receives an `EOF`. When the reader finishes reading, blocks itself, or closes the pipe, all threads in the `tx_wait_queue` are woken up and, next time they reach buffer full, return instead of blocking.

## Fan Driver

The fan driver is Raspberry Pi 5 specific. It is initialized early in
`kernel_main` after UART/framebuffer setup and before the higher-level kernel
subsystems. It is isolated from the rest of the driver framework because it is
not exposed as a user-facing file yet.

The design is intentionally conservative: if fan hardware is not present or the
platform is not Raspberry Pi 5, the rest of the kernel should not depend on fan
support existing.

This is a simple addon added to prevent RPI5 hardware from overheating, and is not a major special feature of this project.

## Driver Initialization Order

Driver initialization is ordered around dependency availability:

1. `uart_init` enables early serial output.
2. `gui_framebuffer_init` prepares framebuffer access.
3. `fan_init` initializes the Raspberry Pi fan path when supported.
4. `irq_init`, `uart_irq_init`, and `timer_init` prepare interrupt-driven input
   and timer events.
5. `block_init` prepares storage.
6. `mount` or `mkfs` brings the filesystem online and registers `/proc` and
   `/dev` virtual root mounts.
7. `initialize_char_device_registry` clears the char-driver table.
8. `uart_char_driver_init` registers the UART char driver.
9. `tty_gui_char_driver_init` registers the TTYGUI char driver.
10. `tty_drivers_init` registers the TTY char driver.
11. `uart_create_device_nodes` populates the `/dev/uart0` devfs node.
12. `tty_gui_create_device_nodes` completes no eager allocation; TTYGUI nodes
    are populated when terminals are activated.
13. `tty_create_device_nodes` marks the TTY layer ready for runtime node
    creation.
14. `tty_create` creates the initial terminal state and populates the
    `tty0`/`ttygui0` devfs nodes.
15. The scheduler starts user processes.

The key design choice is that hardware can be initialized before char-device
nodes exist. The `/dev` mount itself appears when the filesystem is mounted;
user-visible nodes are populated after char drivers register. This keeps early
boot simple while still exposing devices through Unix-style files once the VFS
is available.

## Error Handling and Limitations

Most driver APIs return `0` for success and a negative value or filesystem
error code for failure. The current driver framework is intentionally small.

Current limitations include:

- Static major-number assignment.
- Fixed 16-entry char-driver registry.
- Fixed maximum TTY count.
- Single UART backend instance.
- Single framebuffer terminal renderer.
- Build-time platform selection.
- No hotplug.
- No dynamic module loading.
- No device-tree probing layer.
- No general block-device registry.
- devfs has a fixed-size in-memory node table and no hot-remove API yet.

These are acceptable tradeoffs for the current OS. The driver model stays small
enough to debug, but it already supports devfs nodes, VFS fops,
registered char backends, interrupt-driven input, lazy terminal allocation, and
block storage for the filesystem.
