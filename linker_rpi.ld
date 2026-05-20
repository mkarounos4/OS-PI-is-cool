ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x80000;
    __kernel_start = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : { *(.rodata*) } :text
    .data   : { *(.data*)   } :data

    . = ALIGN(16);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    . = ALIGN(16);
    .stack (NOLOAD) : {
        _stack_bottom = .;
        . += 0x10000;
        _stack_top = .;
    } :data
    
    . = ALIGN(16);
    .kernel_heap (NOLOAD) : {
        __kernel_heap_start = .;
        . += 0x100000; /* 1 MiB for now */
        __kernel_heap_end = .;
    } 

    __kernel_end = .;
}
