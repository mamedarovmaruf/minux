ENTRY(start)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.multiboot)
        *(.text)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data) }
    .bss    : { *(COMMON) *(.bss) }

    . = ALIGN(4096);
    kernel_end = .;
}
