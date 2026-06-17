ENTRY(user_thread_start)

USER_VA_BASE = 0x10000;

SECTIONS
{
    . = USER_VA_BASE;
    __user_image_start = .;

    __user_text_start = .;
    .text : {
        KEEP(*(.text.user_entry))
        *(.text*)
    }
    __user_text_end = .;

    . = ALIGN(4096);
    __user_rodata_start = .;
    .rodata : {
        *(.rodata*)
    }
    __user_rodata_end = .;

    . = ALIGN(4096);
    __user_data_start = .;
    .data : {
        *(.data*)
    }
    __user_data_end = .;

    . = ALIGN(4096);
    __user_bss_start = .;
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }
    __user_bss_end = .;

    . = ALIGN(4096);
    __user_image_end = .;

    __user_init_process_entry = init_process_entry;
    __user_thread_start = user_thread_start;
}
