# Quickstart Guide

### Supported Architecture
- Raspberry Pi 5 Hardware
- Raspberry Pi 3B Emulator through QEMU. Supported on most Mac, Windows, and Linux machines.

# Raspberry Pi 5 Quickstart
The following section includes the steps for building and running this OS on a physical RPI5. For using the QEMU emulator, please look at the `QEMU` section below.
A video covering the following steps may also be found below:

## Required Parts
1. Physical Raspberry Pi 5
2. UART GPIO to USB adaptor (for reading console logging and sending UART input with the RPI5)
3. USB SD card Reader (For reading and loading the kernel code onto the SD)

## Initial Setup
For the first time only, it is required to partition the disk and download the bare minimum firmware needed to boot the code. While we still implemented the boot.S and linker.ld ourselves to load into C code, the focus of this project was not to implement all the drivers by hand, so we need the `BCM2712` processor firmware (to let us run code). 
The formatting is needed to specify a boot section, done in FAT32, which is where our kernel.img, kernel.elf, and config.txt are placed. The rest of the disk will be automaticallly formatted into our own internal filesystem upon boot.

### Step 1: Format the disk
NOTE: If you already have the official RPI OS installed on this SD card, you may skip to `Step 2`.
1. Remove the SD card from the RPI5.
2. Insert the SD card into the SD card reader and plug it into your PC.
3. Download the official Raspberry Pi OS Installer from [this link](!https://www.raspberrypi.com/software/operating-systems/).
4. Click through the prompts until you install either the lite or full RPI5 OS onto this microSD.
5. Open the bootfs of this SD card. You may remove the entire other partition and remove all files in bootfs besides for `BCM2712`. Including or excluding the other files has no impact on this Operating System, so you may remove or add the other files as you wish.

### Step 2: Install our OS on the SD card
1. Download the `kernel_2712.img`, `kernel.elf`, and `config.txt` files from this repository.
2. Follow 1 and 2 from `Step 1` above to open the SD card on your PC.
3. Drag the 3 files from 1 into the `bootfs` on this SD card.

### Step 3: Start the RPI5
1. Remove the SD card from the SD card reader and insert it back into the RPI5
2. Plug in the power of the RPI5 to start it. You may remove power by unplugging it at any time, and restart it by plugging it back in, and the filesystem data will persist.
3. Make sure to plug in the HDMI cable from HDMI slot 0 into any monitor to see the physical GUI terminal.

### UART Input/Output
1. Make sure to plug in a UART adaptor into your computer and plug the RX GPIO line into GPIO PIN 14 and the TX GPIO line into GPIO PIN 15. A photo of this can be found below. This is needed to give UART input to the RPI and get optional debug output.
2a. [LINUX] - install `screen` on your device and run ```sudo screen /dev/ttyUSB<number> 115200``` for the number corresponding to that usb. You may now read UART output through this terminal and, by typing into this terminal, send UART input.
2b. [WINDOWS/MAC] - Navigate to `www.serialterminal.com` and set the `BAUD` rate to `115200`. Then, hit connect and select the USB PORT corresponding to the UART adaptor. You can now read UART output through the terminal and type UART input and hit Send to send commands.

# QEMU Raspberry Pi 3 Emulator

### Step 1: Download QEMU

### Step 2: Make the project
1. Clone this repo with `git clone <url i dont wanna copy it rn>`
2. Run `make qemu` from the directory root
3. 

# Rebuilding the project
If you ever remove the kernel img files or want to make changes and rebuild the project, you will need to adhere to the following steps.
### Step 1: Download aarch64-none-elf compiler

### Step 2: Make the project
1. Clone this repo with `git clone <url i dont wanna copy it rn>`
2. Run `make clone` to make sure there are no stale build artifacts
3. Make the project depending on the architecture.
    - [QEMU] Run `make qemu` to rebuild the project and rum qemu automatically. 
    - [RPI5] Run `make rpi` to rebuild the `kernel_2712.img` and `kernel.elf` files. You may then run it by following `Step 2` onwards of `Raspberry Pi 5 Quickstart`.
