ENTRY(_start)

USER_VA_BASE = 0x10000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = USER_VA_BASE;
    __user_image_start = .;

    __user_text_start = .;
    .text :
    {
        KEEP(*(.text.user_entry))
        *(.text*)
    } :text
    __user_text_end = .;

    . = ALIGN(4096);

    __user_rodata_start = .;
    .rodata :
    {
        *(.rodata*)
    } :rodata
    __user_rodata_end = .;

    . = ALIGN(4096);

    __user_data_start = .;
    .data :
    {
        *(.data*)
    } :data
    __user_data_end = .;

    . = ALIGN(4096);

    __user_bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    } :data
    __user_bss_end = .;

    . = ALIGN(4096);
    __user_image_end = .;

    __user_thread_start = _start;
    __user_init_process_entry = _start;
}
