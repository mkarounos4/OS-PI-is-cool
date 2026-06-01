ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    __kernel_start = .;

    __text_start = .;
    .text : {
        *(.text.boot)
        *(.text*)
    }
    __text_end = .;

    __rodata_start = .;
    .rodata : {
        *(.rodata*)
    }
    __rodata_end = .;

    __data_start = .;
    .data : {
        *(.data*)
    }
    __data_end = .;

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


    . = ALIGN(4096);
    __l0_table = .;
    . += 0x1000;
    __l1_table = .;
    . += 0x1000;
    __l2_table = .;
    . += 0x1000;
    __l3_table = .;
    . += 0x1000;
}
