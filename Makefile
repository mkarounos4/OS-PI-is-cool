CROSS   ?= aarch64-none-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
NM      = $(CROSS)nm
BOOTDIR = /run/media/veerkakar/bootfs
QEMU_SD_IMG ?= build/qemu/sd.img
QEMU_SD_SIZE ?= 1G
UART_OUT ?= 0

PLATFORM ?= rpi

KERNEL_DIR = kernel
BUILD_DIR  = build/$(PLATFORM)
USER_DIR   = user
USER_BUILD_DIR = $(BUILD_DIR)/user
USER_CMD_DIR = $(USER_DIR)/cmds
USER_LIB_DIR = $(USER_DIR)/lib
USER_LINKER = $(USER_DIR)/user_linker.ld
USER_BOOT_SRC = $(USER_DIR)/user_boot.S
USER_BOOT_OBJ = $(USER_BUILD_DIR)/user_boot.S.o
USER_IMAGE_ELF = $(USER_BUILD_DIR)/bin/init.elf
USER_IMAGE_BIN = $(USER_BUILD_DIR)/init.bin
USER_IMAGE_ASM = $(USER_BUILD_DIR)/user_image.S
USER_IMAGE_OBJ = $(USER_BUILD_DIR)/user_image.S.o
USER_IMAGE_HEADER = $(USER_BUILD_DIR)/user_image.h
USER_BINS_ASM = $(USER_BUILD_DIR)/user_bins.S
USER_BINS_C = $(USER_BUILD_DIR)/user_bins.c
USER_BINS_ASM_OBJ = $(USER_BUILD_DIR)/user_bins.S.o
USER_BINS_C_OBJ = $(USER_BUILD_DIR)/user_bins.o

HEADERS  := $(shell find $(KERNEL_DIR) -type f -name '*.h') $(USER_DIR)/user_bins.h $(USER_IMAGE_HEADER)
INCLUDES := $(shell find $(KERNEL_DIR) -type d | sed 's/^/-I/')

CFLAGS = -Wall -Wextra -O2 \
         -ffreestanding -nostdlib -mgeneral-regs-only \
         -fno-pic -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         $(INCLUDES) -I$(USER_BUILD_DIR) -I$(USER_DIR)

USER_CFLAGS = -Wall -Wextra -O2 \
              -ffreestanding -nostdlib -mgeneral-regs-only \
              -fno-pic -fno-pie -fno-stack-protector \
              -fno-asynchronous-unwind-tables -fno-unwind-tables \
              -I$(USER_DIR)

LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
          -Wl,--build-id=none -Wl,-Map=kernel.map

USER_LDFLAGS = -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
               -Wl,--build-id=none

ifeq ($(UART_OUT),1)
    CFLAGS += -DUART_OUT
endif

ifeq ($(PLATFORM),rpi)
    CFLAGS += -DPLATFORM_RPI -DPLATFORM_RPI5 -mcpu=cortex-a76
    UART_SRC = kernel/uart/uart_rpi.c
    GUI_SRC = kernel/gui/gui_rpi.c
    LINKER = linker.ld
    TARGET = kernel_2712.img
else ifeq ($(PLATFORM),qemu)
    CFLAGS += -DPLATFORM_QEMU -mcpu=cortex-a53
    UART_SRC = kernel/uart/uart_qemu.c
    GUI_SRC = kernel/gui/gui_qemu.c
    LINKER = linker.ld
    TARGET = kernel8.img
else
    $(error Unknown PLATFORM '$(PLATFORM)'. Use 'rpi' or 'qemu')
endif

ALL_C_SRCS := $(shell find $(KERNEL_DIR) -type f -name '*.c')
ASM_SRCS   := $(shell find $(KERNEL_DIR) -type f -name '*.S')
USER_LIB_SRCS := $(shell find $(USER_LIB_DIR) -type f -name '*.c')
USER_CMD_SRCS := $(shell find $(USER_CMD_DIR) -maxdepth 1 -type f -name '*.c' | sort)
USER_SHELL_SRCS := $(shell find $(USER_CMD_DIR)/shell -type f -name '*.c' | sort)
USER_CMD_NAMES := $(patsubst $(USER_CMD_DIR)/%.c,%,$(USER_CMD_SRCS))
USER_CMD_ELFS := $(addprefix $(USER_BUILD_DIR)/bin/,$(addsuffix .elf,$(USER_CMD_NAMES)))

C_SRCS := $(filter-out kernel/uart/uart_rpi.c kernel/uart/uart_qemu.c kernel/gui/gui_rpi.c kernel/gui/gui_qemu.c,$(ALL_C_SRCS)) $(UART_SRC) $(GUI_SRC)

OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS += $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.S.o,$(ASM_SRCS))
OBJS += $(USER_IMAGE_OBJ) $(USER_BINS_ASM_OBJ) $(USER_BINS_C_OBJ)

USER_LIB_OBJS := $(patsubst $(USER_DIR)/%.c,$(USER_BUILD_DIR)/%.o,$(USER_LIB_SRCS))
USER_CMD_OBJS := $(patsubst $(USER_DIR)/%.c,$(USER_BUILD_DIR)/%.o,$(USER_CMD_SRCS))
USER_SHELL_OBJS := $(patsubst $(USER_DIR)/%.c,$(USER_BUILD_DIR)/%.o,$(USER_SHELL_SRCS))

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

$(USER_BOOT_OBJ): $(USER_BOOT_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/bin/shell.elf: $(USER_BUILD_DIR)/cmds/shell.o $(USER_SHELL_OBJS) $(USER_LIB_OBJS) $(USER_BOOT_OBJ) $(USER_LINKER)
	@mkdir -p $(dir $@)
	$(CC) -T $(USER_LINKER) $(USER_LDFLAGS) -Wl,-Map=$(USER_BUILD_DIR)/bin/shell.map $(USER_BOOT_OBJ) $(USER_BUILD_DIR)/cmds/shell.o $(USER_SHELL_OBJS) $(USER_LIB_OBJS) -o $@

$(USER_BUILD_DIR)/bin/%.elf: $(USER_BUILD_DIR)/cmds/%.o $(USER_LIB_OBJS) $(USER_BOOT_OBJ) $(USER_LINKER)
	@mkdir -p $(dir $@)
	$(CC) -T $(USER_LINKER) $(USER_LDFLAGS) -Wl,-Map=$(USER_BUILD_DIR)/bin/$*.map $(USER_BOOT_OBJ) $< $(USER_LIB_OBJS) -o $@

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

$(USER_BINS_ASM): $(USER_CMD_ELFS)
	@mkdir -p $(dir $@)
	@printf '.section .rodata.user_bins, "a"\n.balign 8\n' > $@
	@for elf in $(USER_CMD_ELFS); do \
		name=$$(basename $$elf .elf); \
		sym=$$(printf '%s' "$$name" | tr -c 'A-Za-z0-9_' '_'); \
		printf '.global __user_bin_%s_start\n.global __user_bin_%s_end\n' "$$sym" "$$sym" >> $@; \
		elf_abs=$$(cd $$(dirname $$elf) && pwd)/$$(basename $$elf); \
		printf '__user_bin_%s_start:\n.incbin "%s"\n__user_bin_%s_end:\n.balign 8\n' "$$sym" "$$elf" "$$sym" >> $@; \
	done

$(USER_BINS_C): $(USER_CMD_ELFS)
	@mkdir -p $(dir $@)
	@printf '#include "user_bins.h"\n\n' > $@
	@for elf in $(USER_CMD_ELFS); do \
		name=$$(basename $$elf .elf); \
		sym=$$(printf '%s' "$$name" | tr -c 'A-Za-z0-9_' '_'); \
		printf 'extern const uint8_t __user_bin_%s_start[];\nextern const uint8_t __user_bin_%s_end[];\n' "$$sym" "$$sym" >> $@; \
	done
	@printf '\nconst user_bin_t user_bins[] = {\n' >> $@
	@for elf in $(USER_CMD_ELFS); do \
		name=$$(basename $$elf .elf); \
		sym=$$(printf '%s' "$$name" | tr -c 'A-Za-z0-9_' '_'); \
		printf '    { "/bin/%s", __user_bin_%s_start, __user_bin_%s_end },\n' "$$name" "$$sym" "$$sym" >> $@; \
	done
	@printf '};\n\nconst size_t user_bins_count = sizeof(user_bins) / sizeof(user_bins[0]);\n' >> $@

$(USER_BINS_ASM_OBJ): $(USER_BINS_ASM)
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_BINS_C_OBJ): $(USER_BINS_C) $(USER_DIR)/user_bins.h
	$(CC) $(CFLAGS) -c $< -o $@

all: rpi install

rpi:
	$(MAKE) PLATFORM=rpi UART_OUT=$(UART_OUT) build

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
	$(MAKE) PLATFORM=qemu UART_OUT=$(UART_OUT) build
	@mkdir -p $(dir $(QEMU_SD_IMG))
	@test -f $(QEMU_SD_IMG) || truncate -s $(QEMU_SD_SIZE) $(QEMU_SD_IMG)
	qemu-system-aarch64 \
	    -M raspi3b \
	    -cpu cortex-a53 \
	    -display gtk \
	    -serial mon:stdio \
	    -kernel kernel8.img \
	    -drive file=$(QEMU_SD_IMG),if=sd,format=raw

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
