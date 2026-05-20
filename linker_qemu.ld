ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    __kernel_start = .;

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
    
    . = ALIGN(16);
    .kernel_heap (NOLOAD) : {
        __kernel_heap_start = .;
        . += 0x100000; /* 1 MiB for now */
        __kernel_heap_end = .;
    }
    
    __kernel_end = .;
}
