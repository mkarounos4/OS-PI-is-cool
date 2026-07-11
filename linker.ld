ENTRY(_start)

KERNEL_PHYS_BASE = 0x80000;
KERNEL_VA_BASE = 0xffff000000000000;

PHDRS
{
    boot PT_LOAD FLAGS(5);
    bootdata PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_PHYS_BASE;
    __boot_start = .;
    __boot_start_phys = .;

    .text.boot : {
        KEEP(*(.text.boot.entry))
        KEEP(*(.text.boot))
        KEEP(*(.text.boot.*))
        KEEP(*(.text.mmu))
    } :boot

    . = ALIGN(4096);
    .boot_pgtables (NOLOAD) : {
        KEEP(*(.bss.boot_pgtables))
        KEEP(*(.bss.boot_pgtables.*))
    } :bootdata

    . = ALIGN(4096);
    __boot_end = .;
    __boot_end_phys = .;

    . = KERNEL_VA_BASE + __boot_end_phys;
    __kernel_start = .;
    __kernel_start_phys = __kernel_start - KERNEL_VA_BASE;

    __text_start = .;
    .text : AT(ADDR(.text) - KERNEL_VA_BASE) {
        *(.text*)
    } :text

    . = ALIGN(4096);
    __text_end = .;
    __text_start_phys = LOADADDR(.text);
    __text_end_phys = LOADADDR(.text) + SIZEOF(.text);

    __rodata_start = .;
    .rodata : AT(ADDR(.rodata) - KERNEL_VA_BASE) {
        *(.rodata*)
    } :text
    __rodata_end = .;
    __rodata_start_phys = LOADADDR(.rodata);
    __rodata_end_phys = LOADADDR(.rodata) + SIZEOF(.rodata);

    . = ALIGN(4096);
    __user_image_start = .;
    .user_image : AT(ADDR(.user_image) - KERNEL_VA_BASE) {
        KEEP(*(.user_image))
    } :text
    __user_image_end = .;
    __user_image_start_phys = LOADADDR(.user_image);
    __user_image_end_phys = LOADADDR(.user_image) + SIZEOF(.user_image);

    . = ALIGN(4096);
    __data_start = .;
    .data : AT(ADDR(.data) - KERNEL_VA_BASE) {
        *(.data*)
    } :data
    __data_end = .;
    __data_start_phys = LOADADDR(.data);
    __data_end_phys = LOADADDR(.data) + SIZEOF(.data);

    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_VA_BASE) {
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
    } :data
    __bss_end = .;
    __bss_start_phys = LOADADDR(.bss);
    __bss_end_phys = LOADADDR(.bss) + SIZEOF(.bss);

    . = ALIGN(16);
    .stack (NOLOAD) : AT(ADDR(.stack) - KERNEL_VA_BASE) {
        _stack_bottom = .;
        . += 0x10000;
        _stack_top = .;
    } :data
    _stack_bottom_phys = LOADADDR(.stack);
    _stack_top_phys = LOADADDR(.stack) + SIZEOF(.stack);

    . = ALIGN(4096);
    __kernel_end = .;
    __kernel_end_phys = __kernel_end - KERNEL_VA_BASE;

    __kernel_page_pool_start = .;
    __kernel_page_pool_start_phys = __kernel_page_pool_start - KERNEL_VA_BASE;

    __RAM_end_phys = 0x40000000;
    __RAM_end = KERNEL_VA_BASE + __RAM_end_phys;
}
