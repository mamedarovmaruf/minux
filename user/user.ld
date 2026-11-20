/* Flat user program, loaded by the kernel at USER_BASE (see userbin.h). */
ENTRY(_start)

SECTIONS
{
    . = 0xB0000000;
    _image_start = .;

    .header : {
        LONG(0x5855454D)                        /* USER_MAGIC */
        LONG(_start)                            /* entry point */
        LONG(_file_end - _image_start)          /* bytes stored in the file */
        LONG(_mem_end - _image_start)           /* bytes in memory (with bss) */
    }

    .text   : { *(.text._start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    _file_end = .;

    .bss    : { *(.bss .bss.*) *(COMMON) }
    _mem_end = .;

    /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) }
}
