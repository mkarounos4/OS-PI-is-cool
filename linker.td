ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text :
    {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        __bss_end = .;
    }

    . = ALIGN(16);

    .stack :
    {
        . = . + 0x10000;
        _stack_top = .;
    }
}
