/* Places loaded code below video memory and zeroed data above 1 MB. */
ENTRY(_entry16)
SECTIONS
{
    . = 0x8000;
    .stub   : { *(.stub) }
    .text   : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) *(.data.*) }
    . = ALIGN(4);
    __load_end = .;

    . = 0x100000;
    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;

    ASSERT(__load_end  < 0xA0000,  "loaded image (.text/.data) overlaps VGA hole at 0xA0000")
    ASSERT(__bss_end <= 0x190000, "BSS overlaps compact framebuffer at 0x190000")
}
