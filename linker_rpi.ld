/*
 * linker.ld  —  Bare-metal linker script for Raspberry Pi 5 (AArch64)
 *
 * The Pi 5 firmware loads our kernel to 0x80000 (same as Pi 3/4
 * in 64-bit mode) and jumps to the very first instruction there.
 */

ENTRY(_start)

SECTIONS
{
    /* Load address: where the firmware places the binary */
    . = 0x80000;

    .text :
    {
        KEEP(*(.text.boot))     /* boot.S _start must come first */
        *(.text*)
    }

    .rodata : { *(.rodata*) }

    .data   : { *(.data*)   }

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* 64 KiB stack, 16-byte aligned */
    . = ALIGN(16);
    .stack (NOLOAD) :
    {
        . = . + 0x10000;
        _stack_top = .;
    }
}
