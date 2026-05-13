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

SRCS    = src/boot.S src/kernel.c
OBJS    = boot.o kernel.o
TARGET  = kernel8.img

.PHONY: all clean dump

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) -T linker.ld $(OBJS) -o kernel.elf
	$(OBJCOPY) kernel.elf -O binary $@
	@echo "Built $@ ($(shell wc -c < $@) bytes)"

boot.o: src/boot.S
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

# Disassemble for inspection
dump: kernel.elf
	$(OBJDUMP) -d kernel.elf | less

clean:
	rm -f *.o *.elf *.img
