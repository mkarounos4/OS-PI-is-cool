ENTRY(_start)

SECTIONS {
    . = 0x80000;          /* QEMU raspi3b AArch64 entry point */

    .text   : { *(.text.boot) *(.text)   }
    .rodata : { *(.rodata)               }
    .data   : { *(.data)                 }
    .bss    : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    . = ALIGN(16);
    . += 0x4000;            /* 16 KB stack */
    _stack_top = 0x80000;
}
