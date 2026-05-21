CROSS   ?= aarch64-none-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
BOOTDIR = /run/media/veerkakar/bootfs

PLATFORM ?= rpi

KERNEL_DIR = kernel
BUILD_DIR  = build/$(PLATFORM)

HEADERS  := $(shell find $(KERNEL_DIR) -type f -name '*.h')
INCLUDES := $(shell find $(KERNEL_DIR) -type d | sed 's/^/-I/')

CFLAGS = -Wall -Wextra -O2 \
         -ffreestanding -nostdlib -mgeneral-regs-only \
         -fno-pic -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         $(INCLUDES)

LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
          -Wl,--build-id=none -Wl,-Map=kernel.map

ifeq ($(PLATFORM),rpi)
    CFLAGS += -DPLATFORM_RPI -mcpu=cortex-a76
    UART_SRC = kernel/uart/uart_rpi.c
    LINKER = linker_rpi.ld
    TARGET = kernel8.img
else ifeq ($(PLATFORM),qemu)
    CFLAGS += -DPLATFORM_QEMU
    UART_SRC = kernel/uart/uart_qemu.c
    LINKER = linker_qemu.ld
    TARGET = kernel8.img
else
    $(error Unknown PLATFORM '$(PLATFORM)'. Use 'rpi' or 'qemu')
endif

ALL_C_SRCS := $(shell find $(KERNEL_DIR) -type f -name '*.c')
ASM_SRCS   := $(shell find $(KERNEL_DIR) -type f -name '*.S')

C_SRCS := $(filter-out kernel/uart/uart_rpi.c kernel/uart/uart_qemu.c,$(ALL_C_SRCS)) $(UART_SRC)

OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS += $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))

.PHONY: all clean dump rpi install qemu build FORCE

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

all: rpi install

rpi:
	$(MAKE) PLATFORM=rpi build

install:
	cp kernel8.img $(BOOTDIR)
	cp kernel8.img $(BOOTDIR)/kernel_2712.img
	cp kernel.elf $(BOOTDIR)
	cp config.txt $(BOOTDIR)
	@echo "Copied all files to boot dir correctly."
	eject $(BOOTDIR)
	@echo "Ejected boot directory"

# quit qemu with Ctrl+A X
qemu:
	$(MAKE) PLATFORM=qemu build
	qemu-system-aarch64 \
	    -M virt,gic-version=2 \
	    -cpu cortex-a72 \
	    -nographic \
	    -kernel kernel8.img

build: $(TARGET)

$(TARGET): FORCE $(OBJS) $(LINKER)
	$(CC) -T $(LINKER) $(LDFLAGS) $(OBJS) -o kernel.elf
	$(OBJCOPY) kernel.elf -O binary $@
	@echo "Built $@ for PLATFORM=$(PLATFORM) ($$(wc -c < $@) bytes)"

dump: kernel.elf
	$(OBJDUMP) -d kernel.elf | less

clean:
	rm -rf build
	rm -f *.o *.elf *.img *.map

FORCE:
