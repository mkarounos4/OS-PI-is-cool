CROSS = aarch64-none-elf-

CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CFLAGS = -Wall -O2 -ffreestanding -nostdlib

SRC = src/boot.S src/kernel.c

all: kernel8.img

kernel8.img:
	$(CC) $(CFLAGS) -c src/boot.S -o boot.o
	$(CC) $(CFLAGS) -c src/kernel.c -o kernel.o

	$(LD) -T linker.ld boot.o kernel.o -o kernel.elf

	$(OBJCOPY) kernel.elf -O binary kernel8.img

clean:
	rm -f *.o *.elf *.img
