ENTRY(__cc_runtime_entry)

USER_VA_BASE = 0x30000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = USER_VA_BASE;

    .text :
    {
        *(.text*)
    } :text

    . = ALIGN(4096);

    .rodata :
    {
        *(.rodata*)
    } :rodata

    . = ALIGN(4096);

    .data :
    {
        *(.data*)
    } :data

    . = ALIGN(4096);

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    } :data

    . = ALIGN(4096);
}
