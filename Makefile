CROSS   ?= aarch64-none-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
NM      = $(CROSS)nm
BOOTDIR = /run/media/veerkakar/bootfs

PLATFORM ?= rpi

KERNEL_DIR = kernel
BUILD_DIR  = build/$(PLATFORM)
USER_DIR   = user
USER_BUILD_DIR = $(BUILD_DIR)/user
USER_LINKER = $(USER_DIR)/linker.ld
USER_IMAGE_ELF = $(USER_BUILD_DIR)/user.elf
USER_IMAGE_BIN = $(USER_BUILD_DIR)/user.bin
USER_IMAGE_ASM = $(USER_BUILD_DIR)/user_image.S
USER_IMAGE_OBJ = $(USER_BUILD_DIR)/user_image.S.o
USER_IMAGE_HEADER = $(USER_BUILD_DIR)/user_image.h

HEADERS  := $(shell find $(KERNEL_DIR) -type f -name '*.h') $(USER_IMAGE_HEADER)
INCLUDES := $(shell find $(KERNEL_DIR) -type d | sed 's/^/-I/')

CFLAGS = -Wall -Wextra -O2 \
         -ffreestanding -nostdlib -mgeneral-regs-only \
         -fno-pic -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         $(INCLUDES) -I$(USER_BUILD_DIR)

USER_CFLAGS = -Wall -Wextra -O2 \
              -ffreestanding -nostdlib -mgeneral-regs-only \
              -fno-pic -fno-pie -fno-stack-protector \
              -fno-asynchronous-unwind-tables -fno-unwind-tables \
              -I$(USER_DIR)

LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
          -Wl,--build-id=none -Wl,-Map=kernel.map

USER_LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
               -Wl,--build-id=none -Wl,-Map=$(USER_BUILD_DIR)/user.map

ifeq ($(PLATFORM),rpi)
    CFLAGS += -DPLATFORM_RPI -DPLATFORM_RPI5 -mcpu=cortex-a76
    UART_SRC = kernel/uart/uart_rpi.c
    LINKER = linker_rpi.ld
    TARGET = kernel8.img
else ifeq ($(PLATFORM),qemu)
    CFLAGS += -DPLATFORM_QEMU -mcpu=cortex-a53
    UART_SRC = kernel/uart/uart_qemu.c
    LINKER = linker_rpi.ld
    TARGET = kernel8.img
else
    $(error Unknown PLATFORM '$(PLATFORM)'. Use 'rpi' or 'qemu')
endif

ALL_C_SRCS := $(shell find $(KERNEL_DIR) -type f -name '*.c')
ASM_SRCS   := $(shell find $(KERNEL_DIR) -type f -name '*.S')
USER_C_SRCS := $(shell find $(USER_DIR) -type f -name '*.c')

C_SRCS := $(filter-out kernel/uart/uart_rpi.c kernel/uart/uart_qemu.c,$(ALL_C_SRCS)) $(UART_SRC)

OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS += $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.S.o,$(ASM_SRCS))
OBJS += $(USER_IMAGE_OBJ)

USER_OBJS := $(patsubst $(USER_DIR)/%.c,$(USER_BUILD_DIR)/%.o,$(USER_C_SRCS))

.PHONY: all clean dump rpi install qemu build FORCE

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.S.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.o: $(USER_DIR)/%.c $(USER_DIR)/*.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_IMAGE_ELF): $(USER_OBJS) $(USER_LINKER)
	@mkdir -p $(dir $@)
	$(CC) -T $(USER_LINKER) $(USER_LDFLAGS) $(USER_OBJS) -o $@

$(USER_IMAGE_BIN): $(USER_IMAGE_ELF)
	$(OBJCOPY) -O binary $< $@

$(USER_IMAGE_HEADER): $(USER_IMAGE_ELF)
	@mkdir -p $(dir $@)
	@printf '#pragma once\n#include <stdint.h>\n\n' > $@
	@$(NM) -g $< | awk '/ __user_/ { name=$$3; sub(/^__user_/, "USER_", name); printf("#define %s UINT64_C(0x%s)\n", toupper(name), $$1); }' >> $@

$(USER_IMAGE_ASM): $(USER_IMAGE_BIN)
	@mkdir -p $(dir $@)
	@printf '.section .user_image, "a"\n.balign 4096\n.incbin "%s"\n.balign 4096\n' "$(abspath $<)" > $@

$(USER_IMAGE_OBJ): $(USER_IMAGE_ASM)
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
	    -M raspi3b \
	    -cpu cortex-a53 \
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
