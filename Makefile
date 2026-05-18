# Raspberry Pi 5 bare-metal starter
#
# Requires an AArch64 cross toolchain, for example:
#   Ubuntu: sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
#   macOS:  brew install aarch64-none-elf

CROSS   ?= aarch64-none-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

PLATFORM ?= rpi

CFLAGS = -Wall -Wextra -O2 \
         -ffreestanding -nostdlib -mgeneral-regs-only \
         -fno-pic -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
          -Wl,--build-id=none -Wl,-Map=kernel.map

ifeq ($(PLATFORM),rpi)
    CFLAGS += -DPLATFORM_RPI -mcpu=cortex-a76
    UART_SRC = src/uart_rpi.c
    LINKER = linker_rpi.ld
    TARGET = kernel8.img
else ifeq ($(PLATFORM),qemu)
    CFLAGS += -DPLATFORM_QEMU
    UART_SRC = src/uart_qemu.c
    LINKER = linker_qemu.ld
    TARGET = kernel8.img
else
    $(error Unknown PLATFORM '$(PLATFORM)'. Use 'rpi' or 'qemu')
endif

OBJS = boot.o kernel.o uart.o

.PHONY: all clean dump

all: $(TARGET)

$(TARGET): $(OBJS) $(LINKER)
	$(CC) -T $(LINKER) $(LDFLAGS) $(OBJS) -o kernel.elf
	$(OBJCOPY) kernel.elf -O binary $@
	@echo "Built $@ for PLATFORM=$(PLATFORM) ($$(wc -c < $@) bytes)"

boot.o: src/boot.S
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: src/kernel.c src/uart.h
	$(CC) $(CFLAGS) -c $< -o $@

uart.o: $(UART_SRC) src/uart.h
	$(CC) $(CFLAGS) -c $< -o $@

qemu: PLATFORM = qemu
qemu: all
	qemu-system-aarch64 \
	    -M raspi3b \
	    -kernel kernel8.img \
	    -nographic \
	    -monitor none \
	    -semihosting-config enable=on,target=native

dump: kernel.elf
	$(OBJDUMP) -d kernel.elf | less

clean:
	rm -f *.o *.elf *.img *.map
