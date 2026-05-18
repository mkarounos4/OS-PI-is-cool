ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);

    .stack (NOLOAD) : {
        . += 0x4000;
    }

    . = ALIGN(16);
    _stack_top = .;
}
