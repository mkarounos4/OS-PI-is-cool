# Quickstart Guide

This guide covers building OS-PI-is-cool and running it on either QEMU or Raspberry Pi 5 hardware.

## Supported Targets

- Raspberry Pi 5 hardware
- QEMU `raspi3b` machine model for fast emulated development

## Prerequisites

- Git
- Make
- AArch64 bare-metal cross toolchain available as `aarch64-none-elf-gcc`, `aarch64-none-elf-objcopy`, `aarch64-none-elf-objdump`, and `aarch64-none-elf-nm`, if rebuilding project
- QEMU with `qemu-system-aarch64`, if running the emulated target
- Raspberry Pi Imager or an equivalent SD-card imaging tool, if running on Raspberry Pi 5
- USB UART adapter for Raspberry Pi 5 serial input/output
- HDMI display for framebuffer terminal output on Raspberry Pi 5

The Makefile uses `CROSS ?= aarch64-none-elf-`, so a different toolchain prefix can be supplied with `make CROSS=<prefix> ...` if needed.

## Clone the Repository

```sh
git clone https://github.com/mkarounos4/OS-PI-is-cool.git
cd OS-PI-is-cool
```

## Build

Build the Raspberry Pi 5 target:

```sh
make rpi
```

This produces:

- `kernel_2712.img`, the Raspberry Pi 5 boot image
- `kernel.elf`, the linked kernel ELF
- generated userspace ELFs under `build/rpi/user/bin/`

Build the QEMU target without starting QEMU:

```sh
make PLATFORM=qemu build
```

This produces `kernel8.img` for the QEMU `raspi3b` target and generated userspace ELFs under `build/qemu/user/bin/`.

## Run in QEMU

```sh
make qemu
```

The `qemu` target builds with `PLATFORM=qemu`, creates `build/qemu/sd.img` if it does not already exist, and starts:

```text
qemu-system-aarch64 -M raspi3b -cpu cortex-a53 -display gtk -serial mon:stdio -kernel kernel8.img -drive file=build/qemu/sd.img,if=sd,format=raw
```

Keyboard input goes to the QEMU display. Use `Ctrl-A`, then `X`, to quit QEMU from the serial monitor.

## Run on Raspberry Pi 5

Raspberry Pi 5 boot still relies on the board firmware to load the kernel image, as is standard for bare-metal Raspberry Pi development. After firmware handoff, the OS initializes its own kernel entry path, memory layout, drivers, filesystem, and userspace environment.

### Prepare the SD Card

1. Image a microSD card with Raspberry Pi OS using Raspberry Pi Imager or an equivalent tool. The OS does not depend on Linux at runtime, but this creates the FAT boot partition and installs the Raspberry Pi 5 firmware files needed for board startup.
2. Open the card's `bootfs` partition on the development machine.
3. Keep the Raspberry Pi 5 firmware files on the boot partition. The kernel image and `config.txt` from this repository are copied into the same partition.

### Copy the OS Image

Build the Raspberry Pi 5 target:

```sh
make rpi
```

Copy these files to the SD card boot partition:

- `kernel_2712.img`
- `kernel.elf`
- `config.txt`

The provided `config.txt` selects `kernel_2712.img`, enables 64-bit boot, configures the framebuffer, enables UART, and keeps the RP1 PCIe path available for kernel MMIO.

These files are already committed to the repository, so you may skip the `build` step if you do not desire to make any changes.

The repository also includes `build_to_sd`, a helper script that runs `make clean`, builds the Raspberry Pi 5 target, copies the boot files to `/run/media/veerkakar/bootfs/`, syncs, unmounts, and ejects that mount point. Adjust the mount path before using it on a different machine.

### Boot

1. Insert the SD card into the Raspberry Pi 5.
2. Connect HDMI to the first HDMI port for framebuffer terminal output.
3. Connect a USB UART adapter if serial console input/output is needed.
4. Power on the Raspberry Pi 5.

The filesystem is created or mounted by the kernel during boot. On Raspberry Pi 5 media, the filesystem region is selected after the existing boot partition so firmware files remain intact and OS data can persist across reboot.

## UART Input and Output

For Raspberry Pi 5 serial I/O, connect the UART adapter:

- Adapter RX to Raspberry Pi GPIO14/TX
- Adapter TX to Raspberry Pi GPIO15/RX
- Adapter GND to Raspberry Pi GND

Use a serial terminal at `115200` baud. On Linux, for example:

```sh
sudo screen /dev/ttyUSB0 115200
```

The device name may differ, such as `/dev/ttyUSB1` or `/dev/ttyACM0`.

For Windows/Mac as an example, you may use [www.serialterminal.com](www.serialterminal.com).

## Rebuild and Clean

Remove build artifacts:

```sh
make clean
```

Rebuild for Raspberry Pi 5:

```sh
make rpi
```

Rebuild and run QEMU:

```sh
make qemu
```

Build with UART-backed TTY output instead of framebuffer-backed TTY output:

```sh
make UART_OUT=1 rpi
```

or:

```sh
make UART_OUT=1 qemu
```

## Troubleshooting

- Toolchain not found: confirm `aarch64-none-elf-gcc` is installed and on `PATH`, or pass `CROSS=<prefix>` to `make`.
- QEMU target missing: install a QEMU package that includes `qemu-system-aarch64`.
- SD-card copy fails: check the boot partition mount path and write permissions. The default helper script path is machine-specific.
- Serial output is not visible: confirm the adapter wiring, `115200` baud rate, and that `enable_uart=1` is present in `config.txt`.
- Framebuffer output is not visible: use the first HDMI port and confirm the display supports the configured framebuffer mode.
- Stale build artifacts: run `make clean` before rebuilding the target.
