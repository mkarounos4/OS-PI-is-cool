# ==============================================================
#  Makefile — Raspberry Pi 5 bare-metal
#
#  Requires the AArch64 bare-metal toolchain:
#    macOS:   brew install aarch64-none-elf (or arm-none-eabi)
#    Ubuntu:  sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
#             (or download the Arm GNU Toolchain from developer.arm.com)
# ==============================================================

CROSS   = aarch64-none-elf-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS  = -Wall -Wextra -O2 -ffreestanding -nostdlib -mgeneral-regs-only

# ── Platform selection ────────────────────────────────────────────────────────
# Usage:  make PLATFORM=qemu   (default)
#         make PLATFORM=rpi
PLATFORM ?= qemu

ifeq ($(PLATFORM),rpi)
    CFLAGS  += -DPLATFORM_RPI
    UART_SRC = src/uart_rpi.c
else ifeq ($(PLATFORM),qemu)
    CFLAGS  += -DPLATFORM_QEMU
    UART_SRC = src/uart_qemu.c
else
    $(error Unknown PLATFORM '$(PLATFORM)'. Use 'qemu' or 'rpi')
endif

# ── Linker script selection ───────────────────────────────────────────────────
ifeq ($(PLATFORM),rpi)
    LINKER = linker_rpi.ld
else
    LINKER = linker_qemu.ld
endif

# ── Sources & objects ─────────────────────────────────────────────────────────
SRCS = src/boot.S src/kernel.c $(UART_SRC)
OBJS = boot.o kernel.o uart.o

TARGET = kernel8.img

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all clean dump qemu

all: $(TARGET)

$(TARGET): $(OBJS) $(LINKER)
	$(LD) -T $(LINKER) $(OBJS) -o kernel.elf
	$(OBJCOPY) kernel.elf -O binary $@
	@echo "Built $@ for PLATFORM=$(PLATFORM) ($(shell wc -c < $@) bytes)"

boot.o: src/boot.S
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

uart.o: $(UART_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# ── QEMU convenience target ───────────────────────────────────────────────────
# Launches a Raspberry Pi 3B machine; Ctrl-A X to quit
qemu: PLATFORM = qemu
qemu: all
	qemu-system-aarch64 \
	    -M raspi3b \
	    -kernel kernel8.img \
	    -nographic \
	    -monitor none \
	    -semihosting-config enable=on,target=native

# ── Inspection ────────────────────────────────────────────────────────────────
dump: kernel.elf
	$(OBJDUMP) -d kernel.elf | less

clean:
	rm -f *.o *.elf *.img
