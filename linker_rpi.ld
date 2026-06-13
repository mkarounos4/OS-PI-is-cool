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

    __text_start = .;
    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :text
    __text_end = .;

    __rodata_start = .;
    .rodata : { *(.rodata*) } :text
    __rodata_end = .;

    __data_start = .;
    .data   : { *(.data*)   } :data
    __data_end = .;

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

    . = ALIGN(4096);
    __kernel_page_pool_start = .;
    __RAM_end = 0x40000000;
}
